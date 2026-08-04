#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace decompiler {

class ElfLoader;

enum class PatchError {
    None,
    InvalidElf,
    EmptyPatch,
    SizeMismatch,
    InvalidHex,
    NonExecutableAddress,
    OriginalBytesMismatch,
    InvalidInstructionBytes,
    SourceReadFailed,
    OutputExists,
    OriginalOverwriteDenied,
    OutputWriteFailed,
};

struct HexParseResult {
    std::vector<std::uint8_t> bytes;
    std::string errorMessage;

    [[nodiscard]] bool succeeded() const noexcept {
        return errorMessage.empty();
    }
};

struct PatchResult {
    PatchError error = PatchError::None;
    std::string errorMessage;
    std::filesystem::path outputPath;

    [[nodiscard]] bool succeeded() const noexcept {
        return error == PatchError::None;
    }
};

class BinaryPatcher final {
public:
    [[nodiscard]] static HexParseResult parseHexBytes(std::string_view text);
    [[nodiscard]] static std::vector<std::uint8_t> nopBytes(std::size_t count);

    [[nodiscard]] PatchResult patchInstruction(
        const ElfLoader& loader,
        std::uint64_t address,
        std::span<const std::uint8_t> originalBytes,
        std::span<const std::uint8_t> replacementBytes,
        const std::filesystem::path& outputPath,
        bool allowOverwrite = false) const;
};

} // namespace decompiler
