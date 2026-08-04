#include "IRLifter.hpp"

#include "Register.hpp"

#include <capstone/x86.h>

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace decompiler {

static const AbiParameter* parameterForRegister(
    const AbiAnalysisResult& analysis,
    RegisterId registerId) noexcept {
    const auto parameter = std::find_if(
        analysis.parameters.begin(), analysis.parameters.end(), [&](const AbiParameter& value) {
            return value.registerId && *value.registerId == registerId;
        });
    return parameter == analysis.parameters.end() ? nullptr : &*parameter;
}

static const AbiParameter* parameterForStackOffset(
    const AbiAnalysisResult& analysis,
    std::int64_t offset) noexcept {
    const auto parameter = std::find_if(
        analysis.parameters.begin(), analysis.parameters.end(), [&](const AbiParameter& value) {
            return value.stackOffset && *value.stackOffset == offset;
        });
    return parameter == analysis.parameters.end() ? nullptr : &*parameter;
}

static const AbiStackVariable* stackVariableForOffset(
    const AbiAnalysisResult& analysis,
    std::int64_t offset) noexcept {
    const auto variable = std::find_if(
        analysis.stackVariables.begin(),
        analysis.stackVariables.end(),
        [&](const AbiStackVariable& value) { return value.offset == offset; });
    return variable == analysis.stackVariables.end() ? nullptr : &*variable;
}

static std::string hexadecimal(std::uint64_t value) {
    std::ostringstream output;
    output << "0x" << std::hex << std::nouppercase << value;
    return output.str();
}

static std::optional<std::uint64_t> ripRelativeAddress(
    const Instruction& instruction,
    const MemoryOperand& memory) noexcept {
    const auto base = RegisterNormalizer::normalize(memory.baseRegister);
    if(!base || base->id != RegisterId::Rip || !memory.indexRegister.empty()
       || instruction.bytes.size()
              > std::numeric_limits<std::uint64_t>::max() - instruction.address) {
        return std::nullopt;
    }

    const auto nextAddress = instruction.address + instruction.bytes.size();
    if(memory.displacement >= 0) {
        const auto displacement = static_cast<std::uint64_t>(memory.displacement);
        if(displacement > std::numeric_limits<std::uint64_t>::max() - nextAddress) {
            return std::nullopt;
        }
        return nextAddress + displacement;
    }

    const auto magnitude = static_cast<std::uint64_t>(-(memory.displacement + 1)) + 1;
    if(magnitude > nextAddress) {
        return std::nullopt;
    }
    return nextAddress - magnitude;
}

static IRValue registerValue(
    std::string_view name,
    const AbiAnalysisResult& analysis,
    bool destination = false) {
    const auto normalized = RegisterNormalizer::normalize(name);
    if(!normalized) {
        return IRValue {
            .kind = IRValueKind::Unknown,
            .name = std::string(name),
            .type = ValueType::Unknown,
            .bitWidth = 0,
            .registerId = std::nullopt,
            .constant = std::nullopt,
            .stackOffset = std::nullopt,
            .address = std::nullopt,
        };
    }

    if(!destination) {
        if(const auto* parameter = parameterForRegister(analysis, normalized->id)) {
            return IRValue {
                .kind = IRValueKind::Parameter,
                .name = parameter->name,
                .type = parameter->type,
                .bitWidth = parameter->bitWidth,
                .registerId = normalized->id,
                .constant = std::nullopt,
                .stackOffset = std::nullopt,
                .address = std::nullopt,
            };
        }
    }

    const bool pointerRegister = normalized->id == RegisterId::Rbp
                                 || normalized->id == RegisterId::Rsp
                                 || normalized->id == RegisterId::Rip;
    return IRValue {
        .kind = IRValueKind::Register,
        .name = std::string(RegisterNormalizer::canonicalName(normalized->id)),
        .type = pointerRegister ? ValueType::Pointer : ValueType::Integer,
        .bitWidth = normalized->bitWidth,
        .registerId = normalized->id,
        .constant = std::nullopt,
        .stackOffset = std::nullopt,
        .address = std::nullopt,
    };
}

static IRValue immediateValue(const InstructionOperand& operand) {
    return IRValue {
        .kind = IRValueKind::Immediate,
        .name = std::to_string(operand.immediate),
        .type = ValueType::Integer,
        .bitWidth = static_cast<std::uint16_t>(operand.size * 8),
        .registerId = std::nullopt,
        .constant = operand.immediate,
        .stackOffset = std::nullopt,
        .address = std::nullopt,
    };
}

static std::string memoryExpression(
    const MemoryOperand& memory,
    const AbiAnalysisResult& analysis,
    const Instruction* instruction) {
    if(instruction != nullptr) {
        if(const auto address = ripRelativeAddress(*instruction, memory)) {
            return '[' + hexadecimal(*address) + ']';
        }
    }
    std::vector<std::string> components;
    if(!memory.baseRegister.empty()) {
        components.push_back(registerValue(memory.baseRegister, analysis).name);
    }
    if(!memory.indexRegister.empty()) {
        auto index = registerValue(memory.indexRegister, analysis).name;
        if(memory.scale != 1) {
            index += "*" + std::to_string(memory.scale);
        }
        components.push_back(std::move(index));
    }

    std::ostringstream expression;
    expression << '[';
    for(std::size_t index = 0; index < components.size(); ++index) {
        if(index > 0) {
            expression << " + ";
        }
        expression << components[index];
    }
    if(memory.displacement != 0 || components.empty()) {
        if(!components.empty()) {
            expression << (memory.displacement < 0 ? " - " : " + ");
        }
        const auto magnitude = memory.displacement < 0
                                   ? static_cast<std::uint64_t>(-(memory.displacement + 1)) + 1
                                   : static_cast<std::uint64_t>(memory.displacement);
        expression << hexadecimal(magnitude);
    }
    expression << ']';
    return expression.str();
}

static IRValue memoryValue(
    const InstructionOperand& operand,
    const AbiAnalysisResult& analysis,
    const Instruction* instruction) {
    const auto base = RegisterNormalizer::normalize(operand.memory.baseRegister);
    if(base && base->id == RegisterId::Rbp) {
        if(const auto* variable =
               stackVariableForOffset(analysis, operand.memory.displacement)) {
            return IRValue {
                .kind = IRValueKind::StackVariable,
                .name = variable->name,
                .type = variable->type,
                .bitWidth = variable->bitWidth,
                .registerId = std::nullopt,
                .constant = std::nullopt,
                .stackOffset = variable->offset,
                .address = std::nullopt,
            };
        }
        if(const auto* parameter =
               parameterForStackOffset(analysis, operand.memory.displacement)) {
            return IRValue {
                .kind = IRValueKind::Parameter,
                .name = parameter->name,
                .type = parameter->type,
                .bitWidth = parameter->bitWidth,
                .registerId = std::nullopt,
                .constant = std::nullopt,
                .stackOffset = operand.memory.displacement,
                .address = std::nullopt,
            };
        }
    }

    const auto address = instruction == nullptr
                             ? std::optional<std::uint64_t> {}
                             : ripRelativeAddress(*instruction, operand.memory);
    return IRValue {
        .kind = IRValueKind::Memory,
        .name = memoryExpression(operand.memory, analysis, instruction),
        .type = ValueType::Integer,
        .bitWidth = static_cast<std::uint16_t>(operand.size * 8),
        .registerId = std::nullopt,
        .constant = std::nullopt,
        .stackOffset = std::nullopt,
        .address = address,
    };
}

static IRValue operandValue(
    const InstructionOperand& operand,
    const AbiAnalysisResult& analysis,
    bool destination = false,
    const Instruction* instruction = nullptr) {
    switch(operand.kind) {
    case OperandKind::Register:
        return registerValue(operand.registerName, analysis, destination);
    case OperandKind::Immediate:
        return immediateValue(operand);
    case OperandKind::Memory:
        return memoryValue(operand, analysis, instruction);
    case OperandKind::Invalid:
        return {};
    }
    return {};
}

static IRValue targetValue(std::uint64_t target, IRValueKind kind) {
    return IRValue {
        .kind = kind,
        .name = kind == IRValueKind::Function ? "sub_" + hexadecimal(target).substr(2)
                                             : hexadecimal(target),
        .type = ValueType::Pointer,
        .bitWidth = 64,
        .registerId = std::nullopt,
        .constant = std::nullopt,
        .stackOffset = std::nullopt,
        .address = target,
    };
}

static IRValue returnRegisterValue(const AbiAnalysisResult& analysis) {
    return IRValue {
        .kind = IRValueKind::Register,
        .name = "rax",
        .type = analysis.returnType,
        .bitWidth = analysis.returnBitWidth == 0 ? std::uint16_t {64}
                                                 : analysis.returnBitWidth,
        .registerId = RegisterId::Rax,
        .constant = std::nullopt,
        .stackOffset = std::nullopt,
        .address = std::nullopt,
    };
}

static IRInstruction unknownInstruction(const Instruction& instruction) {
    auto comment = instruction.mnemonic;
    if(!instruction.operandText.empty()) {
        comment += " " + instruction.operandText;
    }
    return IRInstruction {
        .opcode = IROpcode::Unknown,
        .destination = std::nullopt,
        .operands = {},
        .sourceAddress = instruction.address,
        .comment = std::move(comment),
    };
}

static IRInstruction binaryInstruction(
    const Instruction& instruction,
    const AbiAnalysisResult& analysis,
    IROpcode opcode) {
    if(instruction.operands.size() < 2) {
        return unknownInstruction(instruction);
    }

    return IRInstruction {
        .opcode = opcode,
        .destination = operandValue(instruction.operands[0], analysis, true, &instruction),
        .operands = {
            operandValue(instruction.operands[0], analysis, false, &instruction),
            operandValue(instruction.operands[1], analysis, false, &instruction),
        },
        .sourceAddress = instruction.address,
        .comment = {},
    };
}

static IRInstruction liftMove(
    const Instruction& instruction,
    const AbiAnalysisResult& analysis) {
    if(instruction.operands.size() < 2) {
        return unknownInstruction(instruction);
    }

    const auto& destinationOperand = instruction.operands[0];
    const auto& sourceOperand = instruction.operands[1];
    auto opcode = IROpcode::Assign;
    if(destinationOperand.kind == OperandKind::Memory) {
        opcode = IROpcode::Store;
    } else if(sourceOperand.kind == OperandKind::Memory) {
        opcode = IROpcode::Load;
    }

    return IRInstruction {
        .opcode = opcode,
        .destination = operandValue(destinationOperand, analysis, true, &instruction),
        .operands = {operandValue(sourceOperand, analysis, false, &instruction)},
        .sourceAddress = instruction.address,
        .comment = {},
    };
}

static IRInstruction liftLea(
    const Instruction& instruction,
    const AbiAnalysisResult& analysis) {
    if(instruction.operands.size() < 2
       || instruction.operands[1].kind != OperandKind::Memory) {
        return unknownInstruction(instruction);
    }

    const auto& memory = instruction.operands[1].memory;
    if(const auto address = ripRelativeAddress(instruction, memory)) {
        auto destination =
            operandValue(instruction.operands[0], analysis, true, &instruction);
        destination.type = ValueType::Pointer;
        return IRInstruction {
            .opcode = IROpcode::Assign,
            .destination = std::move(destination),
            .operands = {targetValue(*address, IRValueKind::Immediate)},
            .sourceAddress = instruction.address,
            .comment = {},
        };
    }
    std::vector<IRValue> components;
    bool pointerResult = false;
    if(!memory.baseRegister.empty()) {
        auto base = registerValue(memory.baseRegister, analysis);
        pointerResult = base.type == ValueType::Pointer;
        components.push_back(std::move(base));
    }
    if(!memory.indexRegister.empty()) {
        auto index = registerValue(memory.indexRegister, analysis);
        pointerResult = pointerResult || index.type == ValueType::Pointer;
        components.push_back(std::move(index));
        if(memory.scale != 1) {
            InstructionOperand scale;
            scale.kind = OperandKind::Immediate;
            scale.size = 4;
            scale.immediate = memory.scale;
            components.push_back(immediateValue(scale));
        }
    }
    if(memory.displacement != 0) {
        InstructionOperand displacement;
        displacement.kind = OperandKind::Immediate;
        displacement.size = 8;
        displacement.immediate = memory.displacement;
        components.push_back(immediateValue(displacement));
    }
    if(components.empty()) {
        components.push_back(memoryValue(instruction.operands[1], analysis, &instruction));
    }

    auto destination = operandValue(instruction.operands[0], analysis, true, &instruction);
    destination.type = pointerResult ? ValueType::Pointer : ValueType::Integer;
    return IRInstruction {
        .opcode = components.size() > 1 ? IROpcode::Add : IROpcode::Assign,
        .destination = std::move(destination),
        .operands = std::move(components),
        .sourceAddress = instruction.address,
        .comment = {},
    };
}

static IRInstruction liftMultiply(
    const Instruction& instruction,
    const AbiAnalysisResult& analysis) {
    if(instruction.operands.empty()) {
        return unknownInstruction(instruction);
    }

    IRInstruction lifted;
    lifted.opcode = IROpcode::Multiply;
    lifted.sourceAddress = instruction.address;
    if(instruction.operands.size() == 1) {
        lifted.destination = returnRegisterValue(analysis);
        lifted.operands = {
            returnRegisterValue(analysis),
            operandValue(instruction.operands[0], analysis, false, &instruction),
        };
    } else if(instruction.operands.size() == 2) {
        lifted.destination = operandValue(instruction.operands[0], analysis, true, &instruction);
        lifted.operands = {
            operandValue(instruction.operands[0], analysis, false, &instruction),
            operandValue(instruction.operands[1], analysis, false, &instruction),
        };
    } else {
        lifted.destination = operandValue(instruction.operands[0], analysis, true, &instruction);
        lifted.operands = {
            operandValue(instruction.operands[1], analysis, false, &instruction),
            operandValue(instruction.operands[2], analysis, false, &instruction),
        };
    }
    return lifted;
}

static IRInstruction liftInstruction(
    const Instruction& instruction,
    const AbiAnalysisResult& analysis) {
    if(instruction.kind == InstructionKind::Invalid) {
        return unknownInstruction(instruction);
    }

    if(instruction.kind == InstructionKind::Call) {
        IRInstruction lifted;
        lifted.opcode = IROpcode::Call;
        lifted.destination = returnRegisterValue(analysis);
        lifted.sourceAddress = instruction.address;
        if(instruction.directTarget) {
            lifted.operands.push_back(
                targetValue(*instruction.directTarget, IRValueKind::Function));
        } else if(!instruction.operands.empty()) {
            lifted.operands.push_back(
                operandValue(instruction.operands[0], analysis, false, &instruction));
        }
        return lifted;
    }

    if(instruction.kind == InstructionKind::Return) {
        IRInstruction lifted;
        lifted.opcode = IROpcode::Return;
        lifted.sourceAddress = instruction.address;
        if(analysis.returnsValue) {
            lifted.operands.push_back(returnRegisterValue(analysis));
        }
        return lifted;
    }

    if(instruction.kind == InstructionKind::ConditionalJump) {
        IRInstruction lifted;
        lifted.opcode = IROpcode::ConditionalJump;
        lifted.sourceAddress = instruction.address;
        if(instruction.directTarget) {
            lifted.operands.push_back(
                targetValue(*instruction.directTarget, IRValueKind::Immediate));
        }
        return lifted;
    }

    if(instruction.kind == InstructionKind::UnconditionalJump
       || instruction.kind == InstructionKind::IndirectJump) {
        IRInstruction lifted;
        lifted.opcode = IROpcode::Jump;
        lifted.sourceAddress = instruction.address;
        if(instruction.directTarget) {
            lifted.operands.push_back(
                targetValue(*instruction.directTarget, IRValueKind::Immediate));
        } else if(!instruction.operands.empty()) {
            lifted.operands.push_back(
                operandValue(instruction.operands[0], analysis, false, &instruction));
        }
        return lifted;
    }

    switch(instruction.architectureId) {
    case X86_INS_MOV:
    case X86_INS_MOVABS:
        return liftMove(instruction, analysis);
    case X86_INS_LEA:
        return liftLea(instruction, analysis);
    case X86_INS_ADD:
        return binaryInstruction(instruction, analysis, IROpcode::Add);
    case X86_INS_SUB:
        return binaryInstruction(instruction, analysis, IROpcode::Subtract);
    case X86_INS_IMUL:
    case X86_INS_MUL:
        return liftMultiply(instruction, analysis);
    case X86_INS_IDIV:
    case X86_INS_DIV: {
        IRInstruction lifted;
        lifted.opcode = IROpcode::Divide;
        lifted.destination = returnRegisterValue(analysis);
        lifted.operands = {returnRegisterValue(analysis)};
        if(!instruction.operands.empty()) {
            lifted.operands.push_back(
                operandValue(instruction.operands[0], analysis, false, &instruction));
        }
        lifted.sourceAddress = instruction.address;
        return lifted;
    }
    case X86_INS_AND:
        return binaryInstruction(instruction, analysis, IROpcode::BitAnd);
    case X86_INS_OR:
        return binaryInstruction(instruction, analysis, IROpcode::BitOr);
    case X86_INS_XOR:
        return binaryInstruction(instruction, analysis, IROpcode::BitXor);
    case X86_INS_SHL:
    case X86_INS_SAL:
        return binaryInstruction(instruction, analysis, IROpcode::ShiftLeft);
    case X86_INS_SHR:
    case X86_INS_SAR:
        return binaryInstruction(instruction, analysis, IROpcode::ShiftRight);
    case X86_INS_CMP:
    case X86_INS_TEST: {
        IRInstruction lifted;
        lifted.opcode = IROpcode::Compare;
        lifted.sourceAddress = instruction.address;
        for(const auto& operand : instruction.operands) {
            lifted.operands.push_back(operandValue(operand, analysis, false, &instruction));
        }
        return lifted;
    }
    case X86_INS_MOVZX:
    case X86_INS_MOVSX:
    case X86_INS_MOVSXD: {
        if(instruction.operands.size() < 2) {
            return unknownInstruction(instruction);
        }
        return IRInstruction {
            .opcode = IROpcode::Cast,
            .destination = operandValue(
                instruction.operands[0], analysis, true, &instruction),
            .operands = {operandValue(
                instruction.operands[1], analysis, false, &instruction)},
            .sourceAddress = instruction.address,
            .comment = {},
        };
    }
    case X86_INS_CDQE:
        return IRInstruction {
            .opcode = IROpcode::Cast,
            .destination = registerValue("rax", analysis, true),
            .operands = {registerValue("eax", analysis)},
            .sourceAddress = instruction.address,
            .comment = {},
        };
    case X86_INS_PUSH: {
        IRValue stackMemory {
            .kind = IRValueKind::Memory,
            .name = "[rsp]",
            .type = ValueType::Integer,
            .bitWidth = 64,
            .registerId = std::nullopt,
            .constant = std::nullopt,
            .stackOffset = std::nullopt,
            .address = std::nullopt,
        };
        IRInstruction lifted;
        lifted.opcode = IROpcode::Store;
        lifted.destination = std::move(stackMemory);
        lifted.sourceAddress = instruction.address;
        if(!instruction.operands.empty()) {
            lifted.operands.push_back(
                operandValue(instruction.operands[0], analysis, false, &instruction));
        }
        return lifted;
    }
    case X86_INS_POP: {
        IRInstruction lifted;
        lifted.opcode = IROpcode::Load;
        lifted.sourceAddress = instruction.address;
        if(!instruction.operands.empty()) {
            lifted.destination =
                operandValue(instruction.operands[0], analysis, true, &instruction);
        }
        lifted.operands.push_back(IRValue {
            .kind = IRValueKind::Memory,
            .name = "[rsp]",
            .type = ValueType::Integer,
            .bitWidth = 64,
            .registerId = std::nullopt,
            .constant = std::nullopt,
            .stackOffset = std::nullopt,
            .address = std::nullopt,
        });
        return lifted;
    }
    case X86_INS_NOP:
        return IRInstruction {
            .opcode = IROpcode::Nop,
            .destination = std::nullopt,
            .operands = {},
            .sourceAddress = instruction.address,
            .comment = {},
        };
    default:
        return unknownInstruction(instruction);
    }
}

IRFunction IRLifter::lift(
    const FunctionInfo& function,
    std::span<const Instruction> instructions,
    const AbiAnalysisResult& abiAnalysis) const {
    IRFunction liftedFunction;
    liftedFunction.address = function.address;
    liftedFunction.name = function.name;
    liftedFunction.returnType = abiAnalysis.returnType;

    for(const auto& parameter : abiAnalysis.parameters) {
        liftedFunction.parameters.push_back(IRValue {
            .kind = IRValueKind::Parameter,
            .name = parameter.name,
            .type = parameter.type,
            .bitWidth = parameter.bitWidth,
            .registerId = parameter.registerId,
            .constant = std::nullopt,
            .stackOffset = parameter.stackOffset,
            .address = std::nullopt,
        });
    }
    for(const auto& variable : abiAnalysis.stackVariables) {
        liftedFunction.localVariables.push_back(IRValue {
            .kind = IRValueKind::StackVariable,
            .name = variable.name,
            .type = variable.type,
            .bitWidth = variable.bitWidth,
            .registerId = std::nullopt,
            .constant = std::nullopt,
            .stackOffset = variable.offset,
            .address = std::nullopt,
        });
    }

    liftedFunction.instructions.reserve(instructions.size());
    for(const auto& instruction : instructions) {
        liftedFunction.instructions.push_back(liftInstruction(instruction, abiAnalysis));
    }
    return liftedFunction;
}

} // namespace decompiler
