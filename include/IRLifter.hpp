#pragma once

#include "AbiAnalyzer.hpp"
#include "FunctionInfo.hpp"
#include "IR.hpp"
#include "Instruction.hpp"

#include <span>

namespace decompiler {

class IRLifter final {
public:
    [[nodiscard]] IRFunction lift(
        const FunctionInfo& function,
        std::span<const Instruction> instructions,
        const AbiAnalysisResult& abiAnalysis) const;
};

} // namespace decompiler

