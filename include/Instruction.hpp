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

enum class OperandKind {
    Register,
    Immediate,
    Memory,
    Invalid,
};

struct MemoryOperand {
    std::string segmentRegister;
    std::string baseRegister;
    std::string indexRegister;
    std::int32_t scale = 1;
    std::int64_t displacement = 0;
};

struct InstructionOperand {
    OperandKind kind = OperandKind::Invalid;
    std::uint8_t size = 0;
    bool isRead = false;
    bool isWritten = false;
    std::string registerName;
    std::int64_t immediate = 0;
    MemoryOperand memory;
};

struct Instruction {
    std::uint64_t address = 0;
    std::vector<std::uint8_t> bytes;
    std::string mnemonic;
    std::string operandText;
    InstructionKind kind = InstructionKind::Normal;
    std::optional<std::uint64_t> directTarget;
    std::uint32_t architectureId = 0;
    std::vector<InstructionOperand> operands;
    std::vector<std::string> registersRead;
    std::vector<std::string> registersWritten;
};

} // namespace decompiler
