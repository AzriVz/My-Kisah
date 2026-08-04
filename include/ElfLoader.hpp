#pragma once

#include "ElfTypes.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace decompiler {

class ElfLoader final {
public:
    bool load(const std::filesystem::path& path);
    void reset() noexcept;

    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] ElfLoadError error() const noexcept;
    [[nodiscard]] std::string_view errorMessage() const noexcept;

    [[nodiscard]] const ElfMetadata& metadata() const noexcept;
    [[nodiscard]] const std::vector<ProgramHeaderInfo>& programHeaders() const noexcept;
    [[nodiscard]] const std::vector<SectionInfo>& sections() const noexcept;
    [[nodiscard]] const std::vector<SymbolInfo>& symbols() const noexcept;

    [[nodiscard]] std::optional<SectionInfo> findSection(std::string_view name) const;
    [[nodiscard]] std::optional<std::uint64_t>
    virtualAddressToFileOffset(std::uint64_t address) const noexcept;
    [[nodiscard]] std::optional<std::uint64_t>
    fileOffsetToVirtualAddress(std::uint64_t offset) const noexcept;

    [[nodiscard]] std::span<const std::uint8_t>
    bytesForSection(std::string_view name) const noexcept;

private:
    bool fail(ElfLoadError error, std::string message);
    bool parse();

    std::vector<std::uint8_t> bytes_;
    ElfMetadata metadata_;
    std::vector<ProgramHeaderInfo> programHeaders_;
    std::vector<SectionInfo> sections_;
    std::vector<SymbolInfo> symbols_;
    ElfLoadError error_ = ElfLoadError::None;
    std::string errorMessage_;
    bool valid_ = false;
};

} // namespace decompiler

