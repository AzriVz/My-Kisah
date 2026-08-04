#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace decompiler {

enum class RegisterId {
    Rax,
    Rbx,
    Rcx,
    Rdx,
    Rsi,
    Rdi,
    Rbp,
    Rsp,
    R8,
    R9,
    R10,
    R11,
    R12,
    R13,
    R14,
    R15,
    Rip,
    Rflags,
};

struct NormalizedRegister {
    RegisterId id = RegisterId::Rax;
    std::uint8_t bitWidth = 0;
    std::uint8_t bitOffset = 0;
    bool zeroExtendsOnWrite = false;
};

class RegisterNormalizer final {
public:
    [[nodiscard]] static std::optional<NormalizedRegister>
    normalize(std::string_view registerName);
    [[nodiscard]] static std::string_view canonicalName(RegisterId id) noexcept;
};

} // namespace decompiler

