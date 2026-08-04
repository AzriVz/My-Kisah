#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace decompiler {

enum class ElfLoadError {
    None,
    FileNotFound,
    CannotOpen,
    EmptyFile,
    FileTooLarge,
    TruncatedHeader,
    InvalidHeader,
    InvalidMagic,
    UnsupportedClass,
    UnsupportedEndianness,
    UnsupportedMachine,
    UnsupportedType,
    InvalidProgramHeaders,
    InvalidSectionHeaders,
    InvalidStringTable,
    InvalidSymbolTable,
    MissingTextSection,
};

struct ElfMetadata {
    std::filesystem::path filePath;
    std::string fileName;
    std::uint64_t fileSize = 0;
    std::uint64_t entryPoint = 0;
    std::uint16_t fileType = 0;
    std::uint16_t machine = 0;
    std::uint8_t osAbi = 0;
    std::size_t programHeaderCount = 0;
    std::size_t sectionCount = 0;
    std::size_t symbolCount = 0;
    bool is64Bit = false;
    bool isLittleEndian = false;
    bool isStripped = true;
    bool isPositionIndependent = false;
};

struct ProgramHeaderInfo {
    std::uint32_t type = 0;
    std::uint32_t flags = 0;
    std::uint64_t fileOffset = 0;
    std::uint64_t virtualAddress = 0;
    std::uint64_t physicalAddress = 0;
    std::uint64_t fileSize = 0;
    std::uint64_t memorySize = 0;
    std::uint64_t alignment = 0;
};

struct SectionInfo {
    std::string name;
    std::uint32_t type = 0;
    std::uint64_t flags = 0;
    std::uint64_t address = 0;
    std::uint64_t fileOffset = 0;
    std::uint64_t size = 0;
    std::uint32_t link = 0;
    std::uint32_t info = 0;
    std::uint64_t alignment = 0;
    std::uint64_t entrySize = 0;
};

struct SymbolInfo {
    std::string name;
    std::uint64_t address = 0;
    std::uint64_t size = 0;
    std::uint16_t sectionIndex = 0;
    std::uint8_t binding = 0;
    std::uint8_t type = 0;
    std::uint8_t visibility = 0;
    bool fromDynamicTable = false;
};

} // namespace decompiler
