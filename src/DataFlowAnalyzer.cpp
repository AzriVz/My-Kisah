#include "DataFlowAnalyzer.hpp"

#include "Register.hpp"

#include <capstone/x86.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace decompiler {

struct ComparisonState {
    std::string left;
    std::string right;
    bool isBitTest = false;
};

struct RecoveryState {
    std::unordered_map<RegisterId, std::string> registerValues;
    std::unordered_map<std::int64_t, std::string> stackValues;
    std::unordered_map<RegisterId, std::string> registerVariables;
    std::unordered_set<RegisterId> availableRegisters;
    std::unordered_set<std::string> variableNames;
    std::size_t nextTemporary = 0;
    std::size_t nextCallResult = 0;
    bool materializeRegisters = false;
};

static constexpr std::array systemVParameterRegisters {
    RegisterId::Rdi,
    RegisterId::Rsi,
    RegisterId::Rdx,
    RegisterId::Rcx,
    RegisterId::R8,
    RegisterId::R9,
};

static std::string hexadecimal(std::uint64_t value) {
    std::ostringstream output;
    output << "0x" << std::hex << std::nouppercase << value;
    return output.str();
}

static bool isFrameRegister(RegisterId registerId) noexcept {
    return registerId == RegisterId::Rbp || registerId == RegisterId::Rsp
           || registerId == RegisterId::Rip || registerId == RegisterId::Rflags;
}

static bool isFrameStackInstruction(const Instruction& instruction) {
    if(instruction.operands.empty()
       || instruction.operands.front().kind != OperandKind::Register) {
        return false;
    }

    const auto registerInfo =
        RegisterNormalizer::normalize(instruction.operands.front().registerName);
    return registerInfo && registerInfo->id == RegisterId::Rbp
           && (instruction.architectureId == X86_INS_PUSH
               || instruction.architectureId == X86_INS_POP);
}

static const Instruction* instructionAt(
    const std::unordered_map<std::uint64_t, const Instruction*>& instructions,
    std::uint64_t address) noexcept {
    const auto position = instructions.find(address);
    return position == instructions.end() ? nullptr : position->second;
}

static const IRInstruction* irAt(
    const std::unordered_map<std::uint64_t, const IRInstruction*>& instructions,
    std::uint64_t address) noexcept {
    const auto position = instructions.find(address);
    return position == instructions.end() ? nullptr : position->second;
}

static const AbiParameter* parameterForRegister(
    const AbiAnalysisResult& analysis,
    RegisterId registerId) noexcept {
    const auto parameter = std::find_if(
        analysis.parameters.begin(), analysis.parameters.end(), [&](const AbiParameter& value) {
            return value.registerId && *value.registerId == registerId;
        });
    return parameter == analysis.parameters.end() ? nullptr : &*parameter;
}

static void addVariable(DataFlowAnalysis& analysis, RecoveryState& state, RecoveredVariable variable) {
    if(!state.variableNames.insert(variable.name).second) {
        return;
    }
    analysis.variables.push_back(std::move(variable));
}

static std::string valueText(const IRValue& value, const RecoveryState& state) {
    if(value.registerId) {
        if(const auto variable = state.registerVariables.find(*value.registerId);
           variable != state.registerVariables.end()) {
            return variable->second;
        }
        if(const auto expression = state.registerValues.find(*value.registerId);
           expression != state.registerValues.end()) {
            return expression->second;
        }
    }

    if(value.kind == IRValueKind::StackVariable && !value.name.empty()) {
        return value.name;
    }
    if(value.stackOffset) {
        if(const auto expression = state.stackValues.find(*value.stackOffset);
           expression != state.stackValues.end()) {
            return expression->second;
        }
    }

    if(value.constant) {
        return std::to_string(*value.constant);
    }
    if(!value.name.empty()) {
        return value.name;
    }
    if(value.address) {
        return hexadecimal(*value.address);
    }
    return "unknown";
}

static std::string registerText(std::string_view name, const RecoveryState& state) {
    const auto normalized = RegisterNormalizer::normalize(name);
    if(!normalized) {
        return std::string(name);
    }
    if(const auto variable = state.registerVariables.find(normalized->id);
       variable != state.registerVariables.end()) {
        return variable->second;
    }
    if(const auto expression = state.registerValues.find(normalized->id);
       expression != state.registerValues.end()) {
        return expression->second;
    }
    return std::string(RegisterNormalizer::canonicalName(normalized->id));
}

static std::string binaryExpression(
    std::string left,
    std::string_view operation,
    std::string right) {
    if((operation == "^" || operation == "-") && left == right) {
        return "0";
    }
    return "(" + std::move(left) + " " + std::string(operation) + " "
           + std::move(right) + ")";
}

static std::string leaExpression(
    const Instruction& instruction,
    const RecoveryState& state) {
    if(instruction.operands.size() < 2
       || instruction.operands[1].kind != OperandKind::Memory) {
        return "unknown";
    }

    const auto& memory = instruction.operands[1].memory;
    std::vector<std::string> components;
    if(!memory.baseRegister.empty()) {
        components.push_back(registerText(memory.baseRegister, state));
    }
    if(!memory.indexRegister.empty()) {
        auto index = registerText(memory.indexRegister, state);
        if(memory.scale != 1) {
            index = "(" + index + " * " + std::to_string(memory.scale) + ")";
        }
        components.push_back(std::move(index));
    }
    if(components.empty()) {
        return std::to_string(memory.displacement);
    }
    auto expression = components.front();
    for(std::size_t index = 1; index < components.size(); ++index) {
        expression = binaryExpression(std::move(expression), "+", components[index]);
    }
    if(memory.displacement < 0) {
        const auto magnitude = static_cast<std::uint64_t>(-(memory.displacement + 1)) + 1;
        expression = binaryExpression(
            std::move(expression), "-", std::to_string(magnitude));
    } else if(memory.displacement > 0) {
        expression = binaryExpression(
            std::move(expression), "+", std::to_string(memory.displacement));
    }
    return expression;
}

static void appendAssignment(
    RecoveredBlock& block,
    std::uint64_t address,
    std::string destination,
    std::string expression) {
    if(destination == expression) {
        return;
    }
    block.statements.push_back(RecoveredStatement {
        .kind = RecoveredStatementKind::Assignment,
        .sourceAddress = address,
        .destination = std::move(destination),
        .expression = std::move(expression),
        .callTarget = std::nullopt,
        .arguments = {},
    });
}

static void writeValue(
    const IRValue& destination,
    std::string expression,
    std::uint64_t address,
    RecoveredBlock& block,
    RecoveryState& state) {
    if(destination.registerId) {
        const auto registerId = *destination.registerId;
        if(isFrameRegister(registerId)) {
            return;
        }

        state.availableRegisters.insert(registerId);
        if(state.materializeRegisters) {
            const auto variable = state.registerVariables.find(registerId);
            if(variable != state.registerVariables.end()) {
                appendAssignment(block, address, variable->second, std::move(expression));
            }
        } else {
            state.registerValues.insert_or_assign(registerId, std::move(expression));
        }
        return;
    }

    if(destination.stackOffset) {
        state.stackValues.insert_or_assign(*destination.stackOffset, expression);
    }
    if(destination.kind == IRValueKind::StackVariable
       || destination.kind == IRValueKind::Memory
       || destination.kind == IRValueKind::Parameter) {
        appendAssignment(block, address, destination.name, std::move(expression));
    }
}

static std::optional<std::string_view> binaryOperator(IROpcode opcode) noexcept {
    switch(opcode) {
    case IROpcode::Add:
        return "+";
    case IROpcode::Subtract:
        return "-";
    case IROpcode::Multiply:
        return "*";
    case IROpcode::Divide:
        return "/";
    case IROpcode::Modulo:
        return "%";
    case IROpcode::BitAnd:
        return "&";
    case IROpcode::BitOr:
        return "|";
    case IROpcode::BitXor:
        return "^";
    case IROpcode::ShiftLeft:
        return "<<";
    case IROpcode::ShiftRight:
        return ">>";
    default:
        return std::nullopt;
    }
}

static std::string testExpression(const ComparisonState& comparison) {
    if(comparison.left == comparison.right) {
        return comparison.left;
    }
    return binaryExpression(comparison.left, "&", comparison.right);
}

static std::string conditionExpression(
    const Instruction& instruction,
    const std::optional<ComparisonState>& comparison) {
    if(!comparison) {
        return "condition_" + hexadecimal(instruction.address).substr(2);
    }

    const auto comparisonText = [&](std::string_view operation) {
        if(comparison->isBitTest) {
            return binaryExpression(testExpression(*comparison), operation, "0");
        }
        return binaryExpression(comparison->left, operation, comparison->right);
    };

    switch(instruction.architectureId) {
    case X86_INS_JE:
        return comparisonText("==");
    case X86_INS_JNE:
        return comparisonText("!=");
    case X86_INS_JL:
    case X86_INS_JB:
        return comparisonText("<");
    case X86_INS_JLE:
    case X86_INS_JBE:
        return comparisonText("<=");
    case X86_INS_JG:
    case X86_INS_JA:
        return comparisonText(">");
    case X86_INS_JGE:
    case X86_INS_JAE:
        return comparisonText(">=");
    case X86_INS_JS:
        return binaryExpression(testExpression(*comparison), "<", "0");
    case X86_INS_JNS:
        return binaryExpression(testExpression(*comparison), ">=", "0");
    case X86_INS_JO:
        return "overflow";
    case X86_INS_JNO:
        return "!overflow";
    case X86_INS_JP:
        return "parity_even";
    case X86_INS_JNP:
        return "!parity_even";
    default:
        return "condition_" + hexadecimal(instruction.address).substr(2);
    }
}

static void prepareRegisterVariables(
    const IRFunction& function,
    const AbiAnalysisResult& abiAnalysis,
    DataFlowAnalysis& analysis,
    RecoveryState& state) {
    for(const auto& instruction : function.instructions) {
        if(!instruction.destination || !instruction.destination->registerId) {
            continue;
        }

        const auto registerId = *instruction.destination->registerId;
        if(isFrameRegister(registerId) || state.registerVariables.contains(registerId)) {
            continue;
        }

        std::string name;
        if(registerId == RegisterId::Rax && abiAnalysis.returnsValue
           && !state.variableNames.contains("result")) {
            name = "result";
        } else {
            do {
                name = "temp" + std::to_string(state.nextTemporary++);
            } while(state.variableNames.contains(name));
        }

        std::optional<std::string> initializer;
        if(const auto* parameter = parameterForRegister(abiAnalysis, registerId)) {
            initializer = parameter->name;
        }
        state.registerVariables.emplace(registerId, name);
        addVariable(
            analysis,
            state,
            RecoveredVariable {
                .name = std::move(name),
                .type = instruction.destination->type,
                .bitWidth = instruction.destination->bitWidth,
                .initializer = std::move(initializer),
            });
    }
}

static std::string callResultName(
    const IRInstruction& instruction,
    DataFlowAnalysis& analysis,
    RecoveryState& state) {
    if(state.materializeRegisters && instruction.destination
       && instruction.destination->registerId) {
        if(const auto variable = state.registerVariables.find(*instruction.destination->registerId);
           variable != state.registerVariables.end()) {
            return variable->second;
        }
    }

    std::string name;
    if(state.nextCallResult == 0 && !state.variableNames.contains("result")) {
        name = "result";
    } else {
        do {
            name = "temp" + std::to_string(state.nextTemporary++);
        } while(state.variableNames.contains(name));
    }
    ++state.nextCallResult;
    addVariable(
        analysis,
        state,
        RecoveredVariable {
            .name = name,
            .type = instruction.destination ? instruction.destination->type : ValueType::Unknown,
            .bitWidth = instruction.destination ? instruction.destination->bitWidth
                                                : std::uint16_t {0},
            .initializer = std::nullopt,
        });
    return name;
}

static void recoverInstruction(
    const IRInstruction& ir,
    const Instruction& instruction,
    const AbiAnalysisResult& abiAnalysis,
    DataFlowAnalysis& analysis,
    RecoveredBlock& block,
    RecoveryState& state,
    std::optional<ComparisonState>& comparison) {
    if(isFrameStackInstruction(instruction)) {
        return;
    }

    if(const auto operation = binaryOperator(ir.opcode)) {
        if(!ir.destination || ir.operands.empty()) {
            return;
        }
        std::string expression;
        if(instruction.architectureId == X86_INS_LEA) {
            expression = leaExpression(instruction, state);
        } else if(ir.operands.size() >= 2) {
            expression = binaryExpression(
                valueText(ir.operands[0], state),
                *operation,
                valueText(ir.operands[1], state));
        } else {
            expression = valueText(ir.operands.front(), state);
        }
        writeValue(*ir.destination, std::move(expression), ir.sourceAddress, block, state);
        return;
    }

    switch(ir.opcode) {
    case IROpcode::Assign:
    case IROpcode::Load:
    case IROpcode::Store:
        if(ir.destination && !ir.operands.empty()) {
            writeValue(
                *ir.destination,
                valueText(ir.operands.front(), state),
                ir.sourceAddress,
                block,
                state);
        }
        break;
    case IROpcode::Cast:
        if(ir.destination && !ir.operands.empty()) {
            const auto targetType = ir.destination->bitWidth <= 32 ? "int" : "long long";
            writeValue(
                *ir.destination,
                "static_cast<" + std::string(targetType) + ">(" 
                    + valueText(ir.operands.front(), state) + ")",
                ir.sourceAddress,
                block,
                state);
        }
        break;
    case IROpcode::Compare:
        if(ir.operands.size() >= 2) {
            comparison = ComparisonState {
                .left = valueText(ir.operands[0], state),
                .right = valueText(ir.operands[1], state),
                .isBitTest = instruction.architectureId == X86_INS_TEST,
            };
        }
        break;
    case IROpcode::Call: {
        std::vector<std::string> arguments(systemVParameterRegisters.size());
        for(std::size_t index = 0; index < systemVParameterRegisters.size(); ++index) {
            const auto registerId = systemVParameterRegisters[index];
            if(!state.availableRegisters.contains(registerId)) {
                continue;
            }
            if(const auto variable = state.registerVariables.find(registerId);
               variable != state.registerVariables.end()) {
                arguments[index] = variable->second;
            } else if(const auto value = state.registerValues.find(registerId);
                      value != state.registerValues.end()) {
                arguments[index] = value->second;
            }
        }

        const auto destination = callResultName(ir, analysis, state);
        block.statements.push_back(RecoveredStatement {
            .kind = RecoveredStatementKind::Call,
            .sourceAddress = ir.sourceAddress,
            .destination = destination,
            .expression = {},
            .callTarget = instruction.directTarget,
            .arguments = std::move(arguments),
        });

        if(!state.materializeRegisters) {
            for(const auto registerId : abiAnalysis.callClobberedRegisters) {
                state.availableRegisters.erase(registerId);
                state.registerValues.erase(registerId);
            }
        }
        state.availableRegisters.insert(RegisterId::Rax);
        if(!state.materializeRegisters) {
            state.registerValues.insert_or_assign(RegisterId::Rax, destination);
        }
        break;
    }
    case IROpcode::Return: {
        auto expression = std::string {};
        if(abiAnalysis.returnsValue) {
            if(!ir.operands.empty()) {
                expression = valueText(ir.operands.front(), state);
            } else if(const auto value = state.registerValues.find(RegisterId::Rax);
                      value != state.registerValues.end()) {
                expression = value->second;
            } else if(const auto variable = state.registerVariables.find(RegisterId::Rax);
                      variable != state.registerVariables.end()) {
                expression = variable->second;
            } else {
                expression = "result";
            }
        }
        block.statements.push_back(RecoveredStatement {
            .kind = RecoveredStatementKind::Return,
            .sourceAddress = ir.sourceAddress,
            .destination = {},
            .expression = std::move(expression),
            .callTarget = std::nullopt,
            .arguments = {},
        });
        break;
    }
    case IROpcode::ConditionalJump:
        block.branchCondition = conditionExpression(instruction, comparison);
        break;
    case IROpcode::Jump:
    case IROpcode::Nop:
        break;
    case IROpcode::Unknown:
        block.statements.push_back(RecoveredStatement {
            .kind = RecoveredStatementKind::Unsupported,
            .sourceAddress = ir.sourceAddress,
            .destination = {},
            .expression = ir.comment.empty() ? instruction.mnemonic : ir.comment,
            .callTarget = std::nullopt,
            .arguments = {},
        });
        break;
    case IROpcode::Phi:
        break;
    case IROpcode::Add:
    case IROpcode::Subtract:
    case IROpcode::Multiply:
    case IROpcode::Divide:
    case IROpcode::Modulo:
    case IROpcode::BitAnd:
    case IROpcode::BitOr:
    case IROpcode::BitXor:
    case IROpcode::ShiftLeft:
    case IROpcode::ShiftRight:
        break;
    }
}

const RecoveredBlock*
DataFlowAnalysis::blockAt(std::uint64_t startAddress) const noexcept {
    const auto block = std::find_if(
        blocks.begin(), blocks.end(), [startAddress](const RecoveredBlock& candidate) {
            return candidate.startAddress == startAddress;
        });
    return block == blocks.end() ? nullptr : &*block;
}

DataFlowAnalysis DataFlowAnalyzer::analyze(
    const IRFunction& function,
    std::span<const Instruction> instructions,
    const ControlFlowGraph& controlFlowGraph,
    const AbiAnalysisResult& abiAnalysis) const {
    DataFlowAnalysis analysis;
    RecoveryState state;
    state.materializeRegisters = controlFlowGraph.blocks().size() > 1;

    for(const auto& parameter : abiAnalysis.parameters) {
        if(!parameter.registerId) {
            continue;
        }
        state.registerValues.insert_or_assign(*parameter.registerId, parameter.name);
        state.availableRegisters.insert(*parameter.registerId);
    }
    if(state.materializeRegisters) {
        prepareRegisterVariables(function, abiAnalysis, analysis, state);
    }

    std::unordered_map<std::uint64_t, const Instruction*> instructionsByAddress;
    instructionsByAddress.reserve(instructions.size());
    for(const auto& instruction : instructions) {
        instructionsByAddress.emplace(instruction.address, &instruction);
    }
    std::unordered_map<std::uint64_t, const IRInstruction*> irByAddress;
    irByAddress.reserve(function.instructions.size());
    for(const auto& instruction : function.instructions) {
        irByAddress.emplace(instruction.sourceAddress, &instruction);
    }

    analysis.blocks.reserve(controlFlowGraph.blocks().size());
    for(const auto& basicBlock : controlFlowGraph.blocks()) {
        RecoveredBlock recoveredBlock;
        recoveredBlock.startAddress = basicBlock.startAddress;
        std::optional<ComparisonState> comparison;
        for(const auto& instruction : basicBlock.instructions) {
            const auto* ir = irAt(irByAddress, instruction.address);
            const auto* original = instructionAt(instructionsByAddress, instruction.address);
            if(ir == nullptr || original == nullptr) {
                recoveredBlock.statements.push_back(RecoveredStatement {
                    .kind = RecoveredStatementKind::Unsupported,
                    .sourceAddress = instruction.address,
                    .destination = {},
                    .expression = "missing IR node",
                    .callTarget = std::nullopt,
                    .arguments = {},
                });
                continue;
            }
            recoverInstruction(
                *ir,
                *original,
                abiAnalysis,
                analysis,
                recoveredBlock,
                state,
                comparison);
        }
        analysis.blocks.push_back(std::move(recoveredBlock));
    }
    return analysis;
}

} // namespace decompiler
