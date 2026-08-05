#include "PseudocodeGenerator.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace decompiler {

struct EmitContext {
    const ControlFlowGraph& controlFlowGraph;
    const DataFlowAnalysis& dataFlowAnalysis;
    std::span<const FunctionPrototype> prototypes;
    std::unordered_set<std::uint64_t> visited;
};

static std::string hexadecimal(std::uint64_t value) {
    std::ostringstream output;
    output << "0x" << std::hex << std::nouppercase << value;
    return output.str();
}

std::string PseudocodeGenerator::identifierForFunction(
    std::string_view name,
    std::uint64_t address) {
    if(name.empty()) {
        return "sub_" + hexadecimal(address).substr(2);
    }

    std::string result;
    result.reserve(name.size() + 1);
    for(const auto character : name) {
        const auto value = static_cast<unsigned char>(character);
        result.push_back(
            std::isalnum(value) != 0 || character == '_' ? character : '_');
    }
    if(result.empty() || std::isdigit(static_cast<unsigned char>(result.front())) != 0) {
        result.insert(result.begin(), '_');
    }
    return result;
}

static std::string typeName(ValueType type, std::uint16_t bitWidth) {
    switch(type) {
    case ValueType::Boolean:
        return "bool";
    case ValueType::Pointer:
        return "void*";
    case ValueType::Integer:
        return bitWidth > 32 ? "long long" : "int";
    case ValueType::Unknown:
        return "long long";
    }
    return "auto";
}

static void indent(std::ostringstream& output, std::size_t depth) {
    output << std::string(depth * 4, ' ');
}

static const FunctionPrototype* prototypeAt(
    std::span<const FunctionPrototype> prototypes,
    std::uint64_t address) noexcept {
    const auto prototype = std::find_if(
        prototypes.begin(), prototypes.end(), [address](const FunctionPrototype& candidate) {
            return candidate.address == address;
        });
    return prototype == prototypes.end() ? nullptr : &*prototype;
}

static std::string callExpression(
    const RecoveredStatement& statement,
    std::span<const FunctionPrototype> prototypes) {
    const auto* prototype = statement.callTarget
                                ? prototypeAt(prototypes, *statement.callTarget)
                                : nullptr;
    const auto callee = prototype != nullptr
                            ? PseudocodeGenerator::identifierForFunction(
                                  prototype->name, prototype->address)
                            : statement.callTarget
                                  ? "sub_" + hexadecimal(*statement.callTarget).substr(2)
                                  : "indirect_call";

    auto argumentCount = std::size_t {0};
    if(prototype != nullptr) {
        argumentCount = prototype->parameterCount;
    } else {
        for(std::size_t index = 0; index < statement.arguments.size(); ++index) {
            if(statement.arguments[index].empty()) {
                break;
            }
            argumentCount = index + 1;
        }
    }

    std::ostringstream output;
    output << callee << '(';
    for(std::size_t index = 0; index < argumentCount; ++index) {
        if(index > 0) {
            output << ", ";
        }
        if(index < statement.arguments.size() && !statement.arguments[index].empty()) {
            output << statement.arguments[index];
        } else {
            output << "/* unknown */";
        }
    }
    output << ')';
    return output.str();
}

static void emitStatement(
    std::ostringstream& output,
    const RecoveredStatement& statement,
    std::size_t depth,
    std::span<const FunctionPrototype> prototypes) {
    indent(output, depth);
    switch(statement.kind) {
    case RecoveredStatementKind::Assignment:
        output << statement.destination << " = " << statement.expression << ";\n";
        break;
    case RecoveredStatementKind::ConditionalAssignment:
        output << "if (" << statement.expression << ") {\n";
        indent(output, depth + 1);
        output << statement.destination << " = "
               << (statement.arguments.empty() ? "/* unknown */" : statement.arguments[0])
               << ";\n";
        indent(output, depth);
        output << "} else {\n";
        indent(output, depth + 1);
        output << statement.destination << " = "
               << (statement.arguments.size() < 2 ? "/* unknown */" : statement.arguments[1])
               << ";\n";
        indent(output, depth);
        output << "}\n";
        break;
    case RecoveredStatementKind::Call: {
        const auto* prototype = statement.callTarget
                                    ? prototypeAt(prototypes, *statement.callTarget)
                                    : nullptr;
        if(prototype == nullptr || prototype->returnsValue) {
            output << statement.destination << " = ";
        }
        output << callExpression(statement, prototypes) << ";\n";
        break;
    }
    case RecoveredStatementKind::Return:
        output << "return";
        if(!statement.expression.empty()) {
            output << ' ' << statement.expression;
        }
        output << ";\n";
        break;
    case RecoveredStatementKind::Unsupported:
        output << "// Unsupported instruction at " << hexadecimal(statement.sourceAddress)
               << ": " << statement.expression << "\n";
        break;
    }
}

static void emitBlockStatements(
    std::ostringstream& output,
    const DataFlowAnalysis& analysis,
    std::uint64_t blockAddress,
    std::size_t depth,
    std::span<const FunctionPrototype> prototypes) {
    const auto* block = analysis.blockAt(blockAddress);
    if(block == nullptr) {
        indent(output, depth);
        output << "// Missing recovered block " << hexadecimal(blockAddress) << "\n";
        return;
    }
    for(const auto& statement : block->statements) {
        emitStatement(output, statement, depth, prototypes);
    }
}

static std::unordered_map<std::uint64_t, std::size_t> reachableDistances(
    const ControlFlowGraph& graph,
    std::uint64_t startAddress) {
    std::unordered_map<std::uint64_t, std::size_t> distances;
    std::queue<std::uint64_t> pending;
    distances.emplace(startAddress, 0);
    pending.push(startAddress);

    while(!pending.empty()) {
        const auto address = pending.front();
        pending.pop();
        const auto* block = graph.blockAt(address);
        if(block == nullptr) {
            continue;
        }
        const auto nextDistance = distances.at(address) + 1;
        for(const auto successor : block->successors) {
            if(distances.emplace(successor, nextDistance).second) {
                pending.push(successor);
            }
        }
    }
    return distances;
}

static std::optional<std::uint64_t> nearestJoin(
    const ControlFlowGraph& graph,
    std::uint64_t left,
    std::uint64_t right) {
    const auto leftDistances = reachableDistances(graph, left);
    const auto rightDistances = reachableDistances(graph, right);
    std::optional<std::uint64_t> bestAddress;
    auto bestMaximumDistance = std::numeric_limits<std::size_t>::max();
    auto bestTotalDistance = std::numeric_limits<std::size_t>::max();

    for(const auto& [address, leftDistance] : leftDistances) {
        const auto rightPosition = rightDistances.find(address);
        if(rightPosition == rightDistances.end()) {
            continue;
        }
        const auto maximumDistance = std::max(leftDistance, rightPosition->second);
        const auto totalDistance = leftDistance + rightPosition->second;
        if(maximumDistance < bestMaximumDistance
           || (maximumDistance == bestMaximumDistance && totalDistance < bestTotalDistance)) {
            bestAddress = address;
            bestMaximumDistance = maximumDistance;
            bestTotalDistance = totalDistance;
        }
    }
    return bestAddress;
}

static std::string branchCondition(
    const DataFlowAnalysis& analysis,
    std::uint64_t blockAddress) {
    const auto* recoveredBlock = analysis.blockAt(blockAddress);
    if(recoveredBlock != nullptr && recoveredBlock->branchCondition) {
        return *recoveredBlock->branchCondition;
    }
    return "condition_" + hexadecimal(blockAddress).substr(2);
}

static void emitStructuredPath(
    std::ostringstream& output,
    std::uint64_t startAddress,
    std::optional<std::uint64_t> stopAddress,
    std::size_t depth,
    EmitContext& context) {
    auto currentAddress = startAddress;
    while(!stopAddress || currentAddress != *stopAddress) {
        if(!context.visited.insert(currentAddress).second) {
            indent(output, depth);
            output << "// Control flow rejoins at block_"
                   << hexadecimal(currentAddress).substr(2) << ".\n";
            return;
        }

        const auto* block = context.controlFlowGraph.blockAt(currentAddress);
        if(block == nullptr) {
            indent(output, depth);
            output << "// Invalid CFG target " << hexadecimal(currentAddress) << "\n";
            return;
        }
        emitBlockStatements(
            output,
            context.dataFlowAnalysis,
            currentAddress,
            depth,
            context.prototypes);

        if(block->successors.empty()) {
            if(block->hasUnresolvedSuccessor) {
                indent(output, depth);
                output << "// Unresolved indirect control-flow target.\n";
            }
            return;
        }
        if(block->successors.size() == 1) {
            currentAddress = block->successors.front();
            continue;
        }

        const auto& terminator = block->instructions.back();
        if(terminator.kind != InstructionKind::ConditionalJump
           || !terminator.directTarget) {
            indent(output, depth);
            output << "// Ambiguous control flow at " << hexadecimal(terminator.address)
                   << "\n";
            return;
        }

        const auto target = *terminator.directTarget;
        const auto fallthrough = block->successors.front() == target
                                     ? block->successors.back()
                                     : block->successors.front();
        const auto condition = branchCondition(context.dataFlowAnalysis, currentAddress);
        const auto join = nearestJoin(context.controlFlowGraph, target, fallthrough);

        if(join && *join == target) {
            indent(output, depth);
            output << "if (!(" << condition << ")) {\n";
            emitStructuredPath(output, fallthrough, join, depth + 1, context);
            indent(output, depth);
            output << "}\n";
            currentAddress = *join;
            continue;
        }
        if(join && *join == fallthrough) {
            indent(output, depth);
            output << "if (" << condition << ") {\n";
            emitStructuredPath(output, target, join, depth + 1, context);
            indent(output, depth);
            output << "}\n";
            currentAddress = *join;
            continue;
        }

        indent(output, depth);
        output << "if (" << condition << ") {\n";
        emitStructuredPath(output, target, join, depth + 1, context);
        indent(output, depth);
        output << "} else {\n";
        emitStructuredPath(output, fallthrough, join, depth + 1, context);
        indent(output, depth);
        output << "}\n";
        if(!join) {
            return;
        }
        currentAddress = *join;
    }
}

static void emitFallbackControlFlow(
    std::ostringstream& output,
    const ControlFlowGraph& graph,
    const DataFlowAnalysis& analysis,
    std::span<const FunctionPrototype> prototypes) {
    for(const auto& block : graph.blocks()) {
        indent(output, 1);
        output << "block_" << hexadecimal(block.startAddress).substr(2) << ":\n";

        const auto& terminator = block.instructions.back();
        const auto condition = branchCondition(analysis, block.startAddress);
        const bool conditional = terminator.kind == InstructionKind::ConditionalJump
                                 && terminator.directTarget
                                 && block.successors.size() == 2;
        std::optional<std::uint64_t> fallthrough;
        if(conditional) {
            fallthrough = block.successors.front() == *terminator.directTarget
                              ? block.successors.back()
                              : block.successors.front();
        }

        const bool jumpBackToSelf = conditional
                                    && *terminator.directTarget == block.startAddress;
        const bool fallBackToSelf = conditional && *fallthrough == block.startAddress;
        if(jumpBackToSelf || fallBackToSelf) {
            indent(output, 2);
            output << "do {\n";
            emitBlockStatements(output, analysis, block.startAddress, 3, prototypes);
            indent(output, 2);
            output << "} while (";
            if(fallBackToSelf) {
                output << "!(" << condition << ')';
            } else {
                output << condition;
            }
            output << ");\n";
            const auto exitAddress = jumpBackToSelf ? *fallthrough : *terminator.directTarget;
            indent(output, 2);
            output << "goto block_" << hexadecimal(exitAddress).substr(2) << ";\n";
            continue;
        }

        emitBlockStatements(output, analysis, block.startAddress, 2, prototypes);
        if(conditional) {
            indent(output, 2);
            output << "if (" << condition << ") {\n";
            indent(output, 3);
            output << "goto block_" << hexadecimal(*terminator.directTarget).substr(2)
                   << ";\n";
            indent(output, 2);
            output << "}\n";
            indent(output, 2);
            output << "goto block_" << hexadecimal(*fallthrough).substr(2) << ";\n";
        } else if(block.successors.size() == 1) {
            indent(output, 2);
            output << "goto block_" << hexadecimal(block.successors.front()).substr(2)
                   << ";\n";
        } else if(block.hasUnresolvedSuccessor) {
            indent(output, 2);
            output << "// Unresolved indirect control-flow target.\n";
        }
        output << '\n';
    }
}

std::string PseudocodeGenerator::generate(
    const FunctionInfo& function,
    const AbiAnalysisResult& abiAnalysis,
    const ControlFlowGraph& controlFlowGraph,
    const DataFlowAnalysis& dataFlowAnalysis,
    std::span<const FunctionPrototype> prototypes) const {
    std::ostringstream output;
    output << "// Reconstructed pseudocode; it may not match the original source.\n";
    output << "// Address: " << hexadecimal(function.address) << "\n";
    output << (abiAnalysis.returnsValue
                   ? typeName(abiAnalysis.returnType, abiAnalysis.returnBitWidth)
                   : "void")
           << ' ' << identifierForFunction(function.name, function.address) << '(';
    for(std::size_t index = 0; index < abiAnalysis.parameters.size(); ++index) {
        if(index > 0) {
            output << ", ";
        }
        const auto& parameter = abiAnalysis.parameters[index];
        output << typeName(parameter.type, parameter.bitWidth) << ' ' << parameter.name;
    }
    output << ") {\n";

    bool hasDeclarations = false;
    for(const auto& localVariable : abiAnalysis.stackVariables) {
        indent(output, 1);
        output << typeName(localVariable.type, localVariable.bitWidth) << ' '
               << localVariable.name << ";\n";
        hasDeclarations = true;
    }
    for(const auto& variable : dataFlowAnalysis.variables) {
        indent(output, 1);
        output << typeName(variable.type, variable.bitWidth) << ' ' << variable.name;
        if(variable.initializer) {
            output << " = " << *variable.initializer;
        }
        output << ";\n";
        hasDeclarations = true;
    }
    if(hasDeclarations) {
        output << '\n';
    }

    if(controlFlowGraph.entryBlock() == nullptr) {
        indent(output, 1);
        output << "// No control-flow entry block is available.\n";
    } else if(controlFlowGraph.backEdges().empty()) {
        EmitContext context {
            .controlFlowGraph = controlFlowGraph,
            .dataFlowAnalysis = dataFlowAnalysis,
            .prototypes = prototypes,
            .visited = {},
        };
        emitStructuredPath(
            output,
            controlFlowGraph.entryBlock()->startAddress,
            std::nullopt,
            1,
            context);
    } else {
        emitFallbackControlFlow(output, controlFlowGraph, dataFlowAnalysis, prototypes);
    }

    output << "}\n";
    return output.str();
}

} // namespace decompiler
