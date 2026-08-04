#pragma once

#include "IR.hpp"
#include "Instruction.hpp"
#include "Register.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace decompiler {

struct AbiParameter {
    std::size_t index = 0;
    std::string name;
    ValueType type = ValueType::Integer;
    std::uint16_t bitWidth = 0;
    std::optional<RegisterId> registerId;
    std::optional<std::int64_t> stackOffset;
};

struct AbiStackVariable {
    std::int64_t offset = 0;
    std::string name;
    ValueType type = ValueType::Integer;
    std::uint16_t bitWidth = 0;
};

struct AbiAnalysisResult {
    std::vector<AbiParameter> parameters;
    std::vector<AbiStackVariable> stackVariables;
    std::vector<RegisterId> callClobberedRegisters;
    bool returnsValue = false;
    ValueType returnType = ValueType::Unknown;
    std::uint16_t returnBitWidth = 0;
};

class AbiAnalyzer final {
public:
    [[nodiscard]] AbiAnalysisResult
    analyze(std::span<const Instruction> instructions) const;

    [[nodiscard]] static std::optional<std::size_t>
    parameterIndex(RegisterId registerId) noexcept;
    [[nodiscard]] static std::string stackVariableName(std::int64_t offset);
};

} // namespace decompiler

