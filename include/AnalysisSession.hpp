#pragma once

#include "AbiAnalyzer.hpp"
#include "ControlFlowGraph.hpp"
#include "Disassembler.hpp"
#include "ElfLoader.hpp"
#include "FunctionInfo.hpp"
#include "IR.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace decompiler {

class AnalysisSession final {
public:
    bool analyze(const std::filesystem::path& path);
    void reset() noexcept;

    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] std::string_view errorMessage() const noexcept;
    [[nodiscard]] const ElfLoader& elfLoader() const noexcept;
    [[nodiscard]] const std::vector<FunctionInfo>& functions() const noexcept;
    [[nodiscard]] const FunctionInfo* functionAt(std::uint64_t address) const noexcept;
    [[nodiscard]] const std::vector<Instruction>*
    instructionsFor(std::uint64_t functionAddress) const noexcept;
    [[nodiscard]] const ControlFlowGraph*
    controlFlowGraphFor(std::uint64_t functionAddress) const noexcept;
    [[nodiscard]] const AbiAnalysisResult*
    abiAnalysisFor(std::uint64_t functionAddress) const noexcept;
    [[nodiscard]] const IRFunction*
    irFor(std::uint64_t functionAddress) const noexcept;

private:
    ElfLoader elfLoader_;
    Disassembler disassembler_;
    std::vector<FunctionInfo> functions_;
    std::unordered_map<std::uint64_t, std::vector<Instruction>> instructionCache_;
    std::unordered_map<std::uint64_t, ControlFlowGraph> controlFlowGraphCache_;
    std::unordered_map<std::uint64_t, AbiAnalysisResult> abiAnalysisCache_;
    std::unordered_map<std::uint64_t, IRFunction> irCache_;
    std::string errorMessage_;
    bool valid_ = false;
};

} // namespace decompiler
