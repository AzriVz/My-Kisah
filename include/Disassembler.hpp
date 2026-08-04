#pragma once

#include "Instruction.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace decompiler {

struct DisassemblyResult {
    std::vector<Instruction> instructions;
    std::string errorMessage;

    [[nodiscard]] bool succeeded() const noexcept {
        return errorMessage.empty();
    }
};

class Disassembler final {
public:
    Disassembler();
    ~Disassembler();

    Disassembler(const Disassembler&) = delete;
    Disassembler& operator=(const Disassembler&) = delete;

    [[nodiscard]] bool isAvailable() const noexcept;
    [[nodiscard]] std::string_view errorMessage() const noexcept;
    [[nodiscard]] DisassemblyResult
    disassemble(std::span<const std::uint8_t> code, std::uint64_t address) const;

private:
    std::size_t handle_ = 0;
    std::string errorMessage_;
};

} // namespace decompiler

