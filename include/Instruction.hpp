#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace decompiler {

enum class InstructionKind {
    Normal,
    Call,
    Return,
    ConditionalJump,
    UnconditionalJump,
    IndirectJump,
    Invalid,
};

struct Instruction {
    std::uint64_t address = 0;
    std::vector<std::uint8_t> bytes;
    std::string mnemonic;
    std::string operandText;
    InstructionKind kind = InstructionKind::Normal;
    std::optional<std::uint64_t> directTarget;
};

} // namespace decompiler

