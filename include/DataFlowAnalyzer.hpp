#pragma once

#include "AbiAnalyzer.hpp"
#include "ControlFlowGraph.hpp"
#include "IR.hpp"
#include "Instruction.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace decompiler {

struct RecoveredVariable {
    std::string name;
    ValueType type = ValueType::Unknown;
    std::uint16_t bitWidth = 0;
    std::optional<std::string> initializer;
};

enum class RecoveredStatementKind {
    Assignment,
    Call,
    Return,
    Unsupported,
};

struct RecoveredStatement {
    RecoveredStatementKind kind = RecoveredStatementKind::Unsupported;
    std::uint64_t sourceAddress = 0;
    std::string destination;
    std::string expression;
    std::optional<std::uint64_t> callTarget;
    std::vector<std::string> arguments;
};

struct RecoveredBlock {
    std::uint64_t startAddress = 0;
    std::vector<RecoveredStatement> statements;
    std::optional<std::string> branchCondition;
};

struct DataFlowAnalysis {
    std::vector<RecoveredVariable> variables;
    std::vector<RecoveredBlock> blocks;

    [[nodiscard]] const RecoveredBlock*
    blockAt(std::uint64_t startAddress) const noexcept;
};

class DataFlowAnalyzer final {
public:
    [[nodiscard]] DataFlowAnalysis analyze(
        const IRFunction& function,
        std::span<const Instruction> instructions,
        const ControlFlowGraph& controlFlowGraph,
        const AbiAnalysisResult& abiAnalysis) const;
};

} // namespace decompiler
