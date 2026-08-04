#include "Register.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace decompiler {

struct RegisterAlias {
    std::string_view name;
    RegisterId id;
    std::uint8_t width;
    std::uint8_t offset;
};

static constexpr std::array registerAliases {
    RegisterAlias {"rax", RegisterId::Rax, 64, 0},
    RegisterAlias {"eax", RegisterId::Rax, 32, 0},
    RegisterAlias {"ax", RegisterId::Rax, 16, 0},
    RegisterAlias {"al", RegisterId::Rax, 8, 0},
    RegisterAlias {"ah", RegisterId::Rax, 8, 8},
    RegisterAlias {"rbx", RegisterId::Rbx, 64, 0},
    RegisterAlias {"ebx", RegisterId::Rbx, 32, 0},
    RegisterAlias {"bx", RegisterId::Rbx, 16, 0},
    RegisterAlias {"bl", RegisterId::Rbx, 8, 0},
    RegisterAlias {"bh", RegisterId::Rbx, 8, 8},
    RegisterAlias {"rcx", RegisterId::Rcx, 64, 0},
    RegisterAlias {"ecx", RegisterId::Rcx, 32, 0},
    RegisterAlias {"cx", RegisterId::Rcx, 16, 0},
    RegisterAlias {"cl", RegisterId::Rcx, 8, 0},
    RegisterAlias {"ch", RegisterId::Rcx, 8, 8},
    RegisterAlias {"rdx", RegisterId::Rdx, 64, 0},
    RegisterAlias {"edx", RegisterId::Rdx, 32, 0},
    RegisterAlias {"dx", RegisterId::Rdx, 16, 0},
    RegisterAlias {"dl", RegisterId::Rdx, 8, 0},
    RegisterAlias {"dh", RegisterId::Rdx, 8, 8},
    RegisterAlias {"rsi", RegisterId::Rsi, 64, 0},
    RegisterAlias {"esi", RegisterId::Rsi, 32, 0},
    RegisterAlias {"si", RegisterId::Rsi, 16, 0},
    RegisterAlias {"sil", RegisterId::Rsi, 8, 0},
    RegisterAlias {"rdi", RegisterId::Rdi, 64, 0},
    RegisterAlias {"edi", RegisterId::Rdi, 32, 0},
    RegisterAlias {"di", RegisterId::Rdi, 16, 0},
    RegisterAlias {"dil", RegisterId::Rdi, 8, 0},
    RegisterAlias {"rbp", RegisterId::Rbp, 64, 0},
    RegisterAlias {"ebp", RegisterId::Rbp, 32, 0},
    RegisterAlias {"bp", RegisterId::Rbp, 16, 0},
    RegisterAlias {"bpl", RegisterId::Rbp, 8, 0},
    RegisterAlias {"rsp", RegisterId::Rsp, 64, 0},
    RegisterAlias {"esp", RegisterId::Rsp, 32, 0},
    RegisterAlias {"sp", RegisterId::Rsp, 16, 0},
    RegisterAlias {"spl", RegisterId::Rsp, 8, 0},
    RegisterAlias {"r8", RegisterId::R8, 64, 0},
    RegisterAlias {"r8d", RegisterId::R8, 32, 0},
    RegisterAlias {"r8w", RegisterId::R8, 16, 0},
    RegisterAlias {"r8b", RegisterId::R8, 8, 0},
    RegisterAlias {"r9", RegisterId::R9, 64, 0},
    RegisterAlias {"r9d", RegisterId::R9, 32, 0},
    RegisterAlias {"r9w", RegisterId::R9, 16, 0},
    RegisterAlias {"r9b", RegisterId::R9, 8, 0},
    RegisterAlias {"r10", RegisterId::R10, 64, 0},
    RegisterAlias {"r10d", RegisterId::R10, 32, 0},
    RegisterAlias {"r10w", RegisterId::R10, 16, 0},
    RegisterAlias {"r10b", RegisterId::R10, 8, 0},
    RegisterAlias {"r11", RegisterId::R11, 64, 0},
    RegisterAlias {"r11d", RegisterId::R11, 32, 0},
    RegisterAlias {"r11w", RegisterId::R11, 16, 0},
    RegisterAlias {"r11b", RegisterId::R11, 8, 0},
    RegisterAlias {"r12", RegisterId::R12, 64, 0},
    RegisterAlias {"r12d", RegisterId::R12, 32, 0},
    RegisterAlias {"r12w", RegisterId::R12, 16, 0},
    RegisterAlias {"r12b", RegisterId::R12, 8, 0},
    RegisterAlias {"r13", RegisterId::R13, 64, 0},
    RegisterAlias {"r13d", RegisterId::R13, 32, 0},
    RegisterAlias {"r13w", RegisterId::R13, 16, 0},
    RegisterAlias {"r13b", RegisterId::R13, 8, 0},
    RegisterAlias {"r14", RegisterId::R14, 64, 0},
    RegisterAlias {"r14d", RegisterId::R14, 32, 0},
    RegisterAlias {"r14w", RegisterId::R14, 16, 0},
    RegisterAlias {"r14b", RegisterId::R14, 8, 0},
    RegisterAlias {"r15", RegisterId::R15, 64, 0},
    RegisterAlias {"r15d", RegisterId::R15, 32, 0},
    RegisterAlias {"r15w", RegisterId::R15, 16, 0},
    RegisterAlias {"r15b", RegisterId::R15, 8, 0},
    RegisterAlias {"rip", RegisterId::Rip, 64, 0},
    RegisterAlias {"eip", RegisterId::Rip, 32, 0},
    RegisterAlias {"ip", RegisterId::Rip, 16, 0},
    RegisterAlias {"rflags", RegisterId::Rflags, 64, 0},
    RegisterAlias {"eflags", RegisterId::Rflags, 32, 0},
    RegisterAlias {"flags", RegisterId::Rflags, 16, 0},
};

std::optional<NormalizedRegister>
RegisterNormalizer::normalize(std::string_view registerName) {
    std::string lowercase(registerName);
    std::transform(lowercase.begin(), lowercase.end(), lowercase.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });

    const auto alias = std::find_if(
        registerAliases.begin(), registerAliases.end(), [&](const RegisterAlias& candidate) {
            return candidate.name == lowercase;
        });
    if(alias == registerAliases.end()) {
        return std::nullopt;
    }

    return NormalizedRegister {
        .id = alias->id,
        .bitWidth = alias->width,
        .bitOffset = alias->offset,
        .zeroExtendsOnWrite = alias->width == 32 && alias->offset == 0,
    };
}

std::string_view RegisterNormalizer::canonicalName(RegisterId id) noexcept {
    switch(id) {
    case RegisterId::Rax:
        return "rax";
    case RegisterId::Rbx:
        return "rbx";
    case RegisterId::Rcx:
        return "rcx";
    case RegisterId::Rdx:
        return "rdx";
    case RegisterId::Rsi:
        return "rsi";
    case RegisterId::Rdi:
        return "rdi";
    case RegisterId::Rbp:
        return "rbp";
    case RegisterId::Rsp:
        return "rsp";
    case RegisterId::R8:
        return "r8";
    case RegisterId::R9:
        return "r9";
    case RegisterId::R10:
        return "r10";
    case RegisterId::R11:
        return "r11";
    case RegisterId::R12:
        return "r12";
    case RegisterId::R13:
        return "r13";
    case RegisterId::R14:
        return "r14";
    case RegisterId::R15:
        return "r15";
    case RegisterId::Rip:
        return "rip";
    case RegisterId::Rflags:
        return "rflags";
    }
    return "unknown";
}

} // namespace decompiler
