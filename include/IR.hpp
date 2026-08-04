#pragma once

#include "Register.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace decompiler {

enum class ValueType {
    Unknown,
    Integer,
    Boolean,
    Pointer,
};

enum class IRValueKind {
    Register,
    Immediate,
    StackVariable,
    Parameter,
    Temporary,
    Memory,
    Function,
    Unknown,
};

enum class IROpcode {
    Assign,
    Load,
    Store,
    Add,
    Subtract,
    Multiply,
    Divide,
    Modulo,
    BitAnd,
    BitOr,
    BitXor,
    ShiftLeft,
    ShiftRight,
    Compare,
    Cast,
    Call,
    Return,
    Jump,
    ConditionalJump,
    Phi,
    Nop,
    Unknown,
};

struct IRValue {
    IRValueKind kind = IRValueKind::Unknown;
    std::string name;
    ValueType type = ValueType::Unknown;
    std::uint16_t bitWidth = 0;
    std::optional<RegisterId> registerId;
    std::optional<std::int64_t> constant;
    std::optional<std::int64_t> stackOffset;
    std::optional<std::uint64_t> address;
};

struct IRInstruction {
    IROpcode opcode = IROpcode::Unknown;
    std::optional<IRValue> destination;
    std::vector<IRValue> operands;
    std::uint64_t sourceAddress = 0;
    std::string comment;
};

struct IRFunction {
    std::uint64_t address = 0;
    std::string name;
    ValueType returnType = ValueType::Unknown;
    std::vector<IRValue> parameters;
    std::vector<IRValue> localVariables;
    std::vector<IRInstruction> instructions;
};

} // namespace decompiler

