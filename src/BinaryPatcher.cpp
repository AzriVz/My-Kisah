#include "BinaryPatcher.hpp"

#include "Disassembler.hpp"
#include "ElfLoader.hpp"

#include <elf.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <limits>
#include <system_error>

namespace decompiler {

static PatchResult patchFailure(PatchError error, std::string message) {
    return PatchResult {
        .error = error,
        .errorMessage = std::move(message),
        .outputPath = {},
    };
}

static int hexadecimalDigit(char value) noexcept {
    if(value >= '0' && value <= '9') {
        return value - '0';
    }
    if(value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if(value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static bool samePath(
    const std::filesystem::path& left,
    const std::filesystem::path& right) noexcept {
    std::error_code error;
    if(std::filesystem::exists(right, error) && !error) {
        const bool equivalent = std::filesystem::equivalent(left, right, error);
        if(!error && equivalent) {
            return true;
        }
    }
    error.clear();
    const auto absoluteLeft = std::filesystem::absolute(left, error).lexically_normal();
    if(error) {
        return left.lexically_normal() == right.lexically_normal();
    }
    const auto absoluteRight = std::filesystem::absolute(right, error).lexically_normal();
    return !error && absoluteLeft == absoluteRight;
}

static const SectionInfo* executableSectionFor(
    const ElfLoader& loader,
    std::uint64_t address,
    std::size_t size) noexcept {
    for(const auto& section : loader.sections()) {
        if((section.flags & SHF_EXECINSTR) == 0 || section.type == SHT_NOBITS
           || address < section.address) {
            continue;
        }
        const auto offset = address - section.address;
        if(offset <= section.size && size <= section.size - offset) {
            return &section;
        }
    }
    return nullptr;
}

HexParseResult BinaryPatcher::parseHexBytes(std::string_view text) {
    std::string digits;
    digits.reserve(text.size());
    for(const auto character : text) {
        const auto byte = static_cast<unsigned char>(character);
        if(std::isspace(byte) != 0) {
            continue;
        }
        if(hexadecimalDigit(character) < 0) {
            return HexParseResult {
                .bytes = {},
                .errorMessage = "Patch contains a non-hexadecimal character.",
            };
        }
        digits.push_back(character);
    }

    if(digits.empty()) {
        return HexParseResult {
            .bytes = {},
            .errorMessage = "Patch bytes cannot be empty.",
        };
    }
    if(digits.size() % 2 != 0) {
        return HexParseResult {
            .bytes = {},
            .errorMessage = "Patch must contain complete two-digit bytes.",
        };
    }

    HexParseResult result;
    result.bytes.reserve(digits.size() / 2);
    for(std::size_t index = 0; index < digits.size(); index += 2) {
        const auto high = hexadecimalDigit(digits[index]);
        const auto low = hexadecimalDigit(digits[index + 1]);
        result.bytes.push_back(static_cast<std::uint8_t>((high << 4) | low));
    }
    return result;
}

std::vector<std::uint8_t> BinaryPatcher::nopBytes(std::size_t count) {
    return std::vector<std::uint8_t>(count, 0x90);
}

PatchResult BinaryPatcher::patchInstruction(
    const ElfLoader& loader,
    std::uint64_t address,
    std::span<const std::uint8_t> originalBytes,
    std::span<const std::uint8_t> replacementBytes,
    const std::filesystem::path& outputPath,
    bool allowOverwrite) const {
    if(!loader.isValid()) {
        return patchFailure(PatchError::InvalidElf, "Patching requires a valid ELF executable.");
    }
    if(originalBytes.empty() || replacementBytes.empty()) {
        return patchFailure(PatchError::EmptyPatch, "Patch bytes cannot be empty.");
    }
    if(originalBytes.size() != replacementBytes.size()) {
        return patchFailure(
            PatchError::SizeMismatch,
            "Replacement byte count must match the original instruction.");
    }
    if(outputPath.empty()) {
        return patchFailure(PatchError::OutputWriteFailed, "An output path is required.");
    }
    if(executableSectionFor(loader, address, replacementBytes.size()) == nullptr) {
        return patchFailure(
            PatchError::NonExecutableAddress,
            "Patch range must remain inside one executable section.");
    }

    const Disassembler disassembler;
    const auto validation = disassembler.disassemble(replacementBytes, address);
    std::size_t decodedSize = 0;
    bool containsInvalidInstruction = false;
    for(const auto& instruction : validation.instructions) {
        decodedSize += instruction.bytes.size();
        containsInvalidInstruction = containsInvalidInstruction
                                     || instruction.kind == InstructionKind::Invalid;
    }
    if(!validation.succeeded() || validation.instructions.empty()
       || decodedSize != replacementBytes.size() || containsInvalidInstruction) {
        return patchFailure(
            PatchError::InvalidInstructionBytes,
            "Replacement bytes do not form a complete x86-64 instruction sequence.");
    }

    const auto fileOffset = loader.virtualAddressToFileOffset(address);
    if(!fileOffset) {
        return patchFailure(
            PatchError::NonExecutableAddress,
            "Instruction address cannot be mapped to the ELF file.");
    }

    const auto& sourcePath = loader.metadata().filePath;
    const bool overwritesOriginal = samePath(sourcePath, outputPath);
    if(overwritesOriginal && !allowOverwrite) {
        return patchFailure(
            PatchError::OriginalOverwriteDenied,
            "The original binary cannot be overwritten without confirmation.");
    }

    std::error_code filesystemError;
    if(!overwritesOriginal && std::filesystem::exists(outputPath, filesystemError)
       && !allowOverwrite) {
        return patchFailure(
            PatchError::OutputExists,
            "The output file already exists and overwrite was not confirmed.");
    }
    if(filesystemError) {
        return patchFailure(PatchError::OutputWriteFailed, "Output file status could not be read.");
    }

    std::ifstream input(sourcePath, std::ios::binary | std::ios::ate);
    if(!input) {
        return patchFailure(PatchError::SourceReadFailed, "Source binary could not be opened.");
    }
    const auto endPosition = input.tellg();
    if(endPosition < 0
       || static_cast<std::uint64_t>(endPosition) > std::numeric_limits<std::size_t>::max()) {
        return patchFailure(PatchError::SourceReadFailed, "Source binary size is invalid.");
    }
    std::vector<std::uint8_t> image(static_cast<std::size_t>(endPosition));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(image.data()), static_cast<std::streamsize>(image.size()));
    if(!input || static_cast<std::size_t>(input.gcount()) != image.size()) {
        return patchFailure(PatchError::SourceReadFailed, "Source binary could not be read fully.");
    }
    if(*fileOffset > image.size() || originalBytes.size() > image.size() - *fileOffset) {
        return patchFailure(PatchError::NonExecutableAddress, "Patch range is outside the file.");
    }

    const auto patchBegin = image.begin() + static_cast<std::ptrdiff_t>(*fileOffset);
    if(!std::equal(originalBytes.begin(), originalBytes.end(), patchBegin)) {
        return patchFailure(
            PatchError::OriginalBytesMismatch,
            "Original bytes changed since the binary was analyzed.");
    }
    std::copy(replacementBytes.begin(), replacementBytes.end(), patchBegin);

    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    if(!output) {
        return patchFailure(PatchError::OutputWriteFailed, "Output binary could not be created.");
    }
    output.write(
        reinterpret_cast<const char*>(image.data()),
        static_cast<std::streamsize>(image.size()));
    output.close();
    if(!output) {
        return patchFailure(PatchError::OutputWriteFailed, "Output binary could not be written.");
    }

    const auto sourcePermissions = std::filesystem::status(sourcePath, filesystemError).permissions();
    if(!filesystemError) {
        std::filesystem::permissions(
            outputPath, sourcePermissions, std::filesystem::perm_options::replace, filesystemError);
    }
    if(filesystemError) {
        return patchFailure(
            PatchError::OutputWriteFailed,
            "Patched binary was written, but its executable permissions could not be preserved.");
    }

    return PatchResult {
        .error = PatchError::None,
        .errorMessage = {},
        .outputPath = outputPath,
    };
}

} // namespace decompiler
