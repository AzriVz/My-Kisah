#pragma once

#include "AbiAnalyzer.hpp"
#include "ControlFlowGraph.hpp"
#include "DataFlowAnalyzer.hpp"
#include "FunctionInfo.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace decompiler {

struct FunctionPrototype {
    std::uint64_t address = 0;
    std::string name;
    std::size_t parameterCount = 0;
    bool returnsValue = false;
    ValueType returnType = ValueType::Unknown;
    std::uint16_t returnBitWidth = 0;
};

class PseudocodeGenerator final {
public:
    [[nodiscard]] std::string generate(
        const FunctionInfo& function,
        const AbiAnalysisResult& abiAnalysis,
        const ControlFlowGraph& controlFlowGraph,
        const DataFlowAnalysis& dataFlowAnalysis,
        std::span<const FunctionPrototype> prototypes) const;
};

} // namespace decompiler
