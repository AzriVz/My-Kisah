#include "ElfLoader.hpp"

#include <elf.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <utility>

namespace decompiler {

template<typename T>
static std::optional<T>
readObject(std::span<const std::uint8_t> bytes, std::uint64_t offset) {
    if(offset > bytes.size() || sizeof(T) > bytes.size() - offset) {
        return std::nullopt;
    }

    T value {};
    std::memcpy(&value, bytes.data() + static_cast<std::size_t>(offset), sizeof(T));
    return value;
}

static bool rangeFits(
    std::span<const std::uint8_t> bytes,
    std::uint64_t offset,
    std::uint64_t size) noexcept {
    return offset <= bytes.size() && size <= bytes.size() - offset;
}

static bool tableFits(
    std::span<const std::uint8_t> bytes,
    std::uint64_t offset,
    std::uint64_t entrySize,
    std::uint64_t entryCount) noexcept {
    if(entrySize == 0 || offset > bytes.size()) {
        return false;
    }

    return entryCount <= (bytes.size() - offset) / entrySize;
}

static std::optional<std::string> readString(
    std::span<const std::uint8_t> stringTable,
    std::uint64_t offset) {
    if(offset >= stringTable.size()) {
        return std::nullopt;
    }

    const auto begin = stringTable.begin() + static_cast<std::ptrdiff_t>(offset);
    const auto end = std::find(begin, stringTable.end(), std::uint8_t {0});
    if(end == stringTable.end()) {
        return std::nullopt;
    }

    return std::string(
        reinterpret_cast<const char*>(&*begin),
        static_cast<std::size_t>(std::distance(begin, end)));
}

static bool contains(std::uint64_t start, std::uint64_t size, std::uint64_t value) noexcept {
    return value >= start && value - start < size;
}

bool ElfLoader::load(const std::filesystem::path& path) {
    reset();

    std::error_code filesystemError;
    const bool fileExists = std::filesystem::exists(path, filesystemError);
    if(filesystemError) {
        return fail(ElfLoadError::CannotOpen, "File status could not be read.");
    }

    if(!fileExists) {
        return fail(ElfLoadError::FileNotFound, "File does not exist.");
    }

    const bool isRegularFile = std::filesystem::is_regular_file(path, filesystemError);
    if(filesystemError || !isRegularFile) {
        return fail(ElfLoadError::CannotOpen, "Path is not a readable regular file.");
    }

    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if(!input) {
        return fail(ElfLoadError::CannotOpen, "File could not be opened for reading.");
    }

    const auto endPosition = input.tellg();
    if(endPosition < 0) {
        return fail(ElfLoadError::CannotOpen, "File size could not be determined.");
    }

    const auto fileSize = static_cast<std::uint64_t>(endPosition);
    if(fileSize == 0) {
        return fail(ElfLoadError::EmptyFile, "File is empty.");
    }

    if(fileSize > std::numeric_limits<std::size_t>::max()
       || fileSize > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max())) {
        return fail(ElfLoadError::FileTooLarge, "File is too large to load safely.");
    }

    bytes_.resize(static_cast<std::size_t>(fileSize));
    input.seekg(0, std::ios::beg);
    input.read(
        reinterpret_cast<char*>(bytes_.data()),
        static_cast<std::streamsize>(bytes_.size()));

    if(!input || static_cast<std::size_t>(input.gcount()) != bytes_.size()) {
        return fail(ElfLoadError::CannotOpen, "The complete file could not be read.");
    }

    const auto absolutePath = std::filesystem::absolute(path, filesystemError);
    metadata_.filePath = filesystemError ? path : absolutePath.lexically_normal();
    metadata_.fileName = path.filename().string();
    metadata_.fileSize = fileSize;

    return parse();
}

void ElfLoader::reset() noexcept {
    bytes_.clear();
    metadata_ = {};
    programHeaders_.clear();
    sections_.clear();
    symbols_.clear();
    error_ = ElfLoadError::None;
    errorMessage_.clear();
    valid_ = false;
}

bool ElfLoader::isValid() const noexcept {
    return valid_;
}

ElfLoadError ElfLoader::error() const noexcept {
    return error_;
}

std::string_view ElfLoader::errorMessage() const noexcept {
    return errorMessage_;
}

const ElfMetadata& ElfLoader::metadata() const noexcept {
    return metadata_;
}

const std::vector<ProgramHeaderInfo>& ElfLoader::programHeaders() const noexcept {
    return programHeaders_;
}

const std::vector<SectionInfo>& ElfLoader::sections() const noexcept {
    return sections_;
}

const std::vector<SymbolInfo>& ElfLoader::symbols() const noexcept {
    return symbols_;
}

std::optional<SectionInfo> ElfLoader::findSection(std::string_view name) const {
    const auto section = std::find_if(
        sections_.begin(), sections_.end(), [name](const SectionInfo& candidate) {
            return candidate.name == name;
        });

    if(section == sections_.end()) {
        return std::nullopt;
    }

    return *section;
}

std::optional<std::uint64_t>
ElfLoader::virtualAddressToFileOffset(std::uint64_t address) const noexcept {
    for(const auto& segment : programHeaders_) {
        if(segment.type != PT_LOAD || !contains(segment.virtualAddress, segment.fileSize, address)) {
            continue;
        }

        return segment.fileOffset + (address - segment.virtualAddress);
    }

    for(const auto& section : sections_) {
        if((section.flags & SHF_ALLOC) == 0 || section.type == SHT_NOBITS
           || !contains(section.address, section.size, address)) {
            continue;
        }

        return section.fileOffset + (address - section.address);
    }

    return std::nullopt;
}

std::optional<std::uint64_t>
ElfLoader::fileOffsetToVirtualAddress(std::uint64_t offset) const noexcept {
    for(const auto& segment : programHeaders_) {
        if(segment.type != PT_LOAD || !contains(segment.fileOffset, segment.fileSize, offset)) {
            continue;
        }

        return segment.virtualAddress + (offset - segment.fileOffset);
    }

    for(const auto& section : sections_) {
        if((section.flags & SHF_ALLOC) == 0 || section.type == SHT_NOBITS
           || !contains(section.fileOffset, section.size, offset)) {
            continue;
        }

        return section.address + (offset - section.fileOffset);
    }

    return std::nullopt;
}

std::span<const std::uint8_t>
ElfLoader::bytesForSection(std::string_view name) const noexcept {
    const auto section = std::find_if(
        sections_.begin(), sections_.end(), [name](const SectionInfo& candidate) {
            return candidate.name == name;
        });

    if(section == sections_.end() || section->type == SHT_NOBITS || section->size == 0
       || !rangeFits(bytes_, section->fileOffset, section->size)) {
        return {};
    }

    return std::span<const std::uint8_t>(
        bytes_.data() + static_cast<std::size_t>(section->fileOffset),
        static_cast<std::size_t>(section->size));
}

bool ElfLoader::fail(ElfLoadError error, std::string message) {
    bytes_.clear();
    metadata_ = {};
    programHeaders_.clear();
    sections_.clear();
    symbols_.clear();
    error_ = error;
    errorMessage_ = std::move(message);
    valid_ = false;
    return false;
}

bool ElfLoader::parse() {
    const std::span<const std::uint8_t> fileBytes(bytes_);
    if(fileBytes.size() < EI_NIDENT) {
        return fail(ElfLoadError::TruncatedHeader, "File is too small to contain an ELF header.");
    }

    if(fileBytes[EI_MAG0] != ELFMAG0 || fileBytes[EI_MAG1] != ELFMAG1
       || fileBytes[EI_MAG2] != ELFMAG2 || fileBytes[EI_MAG3] != ELFMAG3) {
        return fail(ElfLoadError::InvalidMagic, "File does not have the ELF magic bytes.");
    }

    if(fileBytes[EI_CLASS] != ELFCLASS64) {
        return fail(ElfLoadError::UnsupportedClass, "Only ELF 64-bit files are supported.");
    }

    if(fileBytes[EI_DATA] != ELFDATA2LSB) {
        return fail(
            ElfLoadError::UnsupportedEndianness,
            "Only little-endian ELF files are supported.");
    }

    if(fileBytes[EI_VERSION] != EV_CURRENT) {
        return fail(ElfLoadError::InvalidHeader, "ELF identification version is invalid.");
    }

    const auto header = readObject<Elf64_Ehdr>(fileBytes, 0);
    if(!header || header->e_ehsize < sizeof(Elf64_Ehdr)
       || header->e_ehsize > fileBytes.size()) {
        return fail(ElfLoadError::TruncatedHeader, "ELF header is truncated or malformed.");
    }

    if(header->e_version != EV_CURRENT) {
        return fail(ElfLoadError::InvalidHeader, "ELF header version is invalid.");
    }

    if(header->e_machine != EM_X86_64) {
        return fail(ElfLoadError::UnsupportedMachine, "Only x86-64 ELF files are supported.");
    }

    if(header->e_type != ET_EXEC) {
        return fail(
            ElfLoadError::UnsupportedType,
            "Only non-PIE ELF executables are supported in this version.");
    }

    std::uint64_t sectionCount = header->e_shnum;
    std::uint64_t sectionNameIndex = header->e_shstrndx;
    std::uint64_t programHeaderCount = header->e_phnum;

    const bool needsSectionZero = sectionCount == 0 || sectionNameIndex == SHN_XINDEX
                                  || programHeaderCount == PN_XNUM;
    std::optional<Elf64_Shdr> sectionZero;
    if(needsSectionZero) {
        if(header->e_shoff == 0 || header->e_shentsize < sizeof(Elf64_Shdr)) {
            return fail(
                ElfLoadError::InvalidSectionHeaders,
                "Extended ELF header counts require a valid section table.");
        }

        sectionZero = readObject<Elf64_Shdr>(fileBytes, header->e_shoff);
        if(!sectionZero) {
            return fail(
                ElfLoadError::InvalidSectionHeaders,
                "Section table header is outside the file.");
        }

        if(sectionCount == 0) {
            sectionCount = sectionZero->sh_size;
        }
        if(sectionNameIndex == SHN_XINDEX) {
            sectionNameIndex = sectionZero->sh_link;
        }
        if(programHeaderCount == PN_XNUM) {
            programHeaderCount = sectionZero->sh_info;
        }
    }

    if(sectionCount == 0 || header->e_shoff == 0
       || header->e_shentsize < sizeof(Elf64_Shdr)
       || !tableFits(fileBytes, header->e_shoff, header->e_shentsize, sectionCount)) {
        return fail(ElfLoadError::InvalidSectionHeaders, "Section header table is invalid.");
    }

    if(sectionNameIndex == SHN_UNDEF || sectionNameIndex >= sectionCount) {
        return fail(
            ElfLoadError::InvalidStringTable,
            "Section-name string table index is invalid.");
    }

    std::vector<Elf64_Shdr> rawSections;
    rawSections.reserve(static_cast<std::size_t>(sectionCount));
    for(std::uint64_t index = 0; index < sectionCount; ++index) {
        const auto rawSection = readObject<Elf64_Shdr>(
            fileBytes, header->e_shoff + index * header->e_shentsize);
        if(!rawSection) {
            return fail(
                ElfLoadError::InvalidSectionHeaders,
                "A section header is outside the file.");
        }

        if(rawSection->sh_type != SHT_NOBITS
           && !rangeFits(fileBytes, rawSection->sh_offset, rawSection->sh_size)) {
            return fail(
                ElfLoadError::InvalidSectionHeaders,
                "A section points outside the file.");
        }

        if((rawSection->sh_flags & SHF_ALLOC) != 0
           && rawSection->sh_size
                  > std::numeric_limits<std::uint64_t>::max() - rawSection->sh_addr) {
            return fail(
                ElfLoadError::InvalidSectionHeaders,
                "An allocated section has an overflowing virtual-address range.");
        }

        rawSections.push_back(*rawSection);
    }

    const auto& rawSectionNames = rawSections[static_cast<std::size_t>(sectionNameIndex)];
    if(rawSectionNames.sh_type != SHT_STRTAB
       || !rangeFits(fileBytes, rawSectionNames.sh_offset, rawSectionNames.sh_size)) {
        return fail(
            ElfLoadError::InvalidStringTable,
            "Section-name string table is invalid.");
    }

    const auto sectionNames = fileBytes.subspan(
        static_cast<std::size_t>(rawSectionNames.sh_offset),
        static_cast<std::size_t>(rawSectionNames.sh_size));

    sections_.reserve(rawSections.size());
    for(const auto& rawSection : rawSections) {
        const auto name = readString(sectionNames, rawSection.sh_name);
        if(!name) {
            return fail(
                ElfLoadError::InvalidStringTable,
                "A section name is outside its string table.");
        }

        sections_.push_back(SectionInfo {
            .name = *name,
            .type = rawSection.sh_type,
            .flags = rawSection.sh_flags,
            .address = rawSection.sh_addr,
            .fileOffset = rawSection.sh_offset,
            .size = rawSection.sh_size,
            .link = rawSection.sh_link,
            .info = rawSection.sh_info,
            .alignment = rawSection.sh_addralign,
            .entrySize = rawSection.sh_entsize,
        });
    }

    const auto textSection = findSection(".text");
    if(!textSection || textSection->type == SHT_NOBITS
       || (textSection->flags & SHF_EXECINSTR) == 0 || textSection->size == 0) {
        return fail(
            ElfLoadError::MissingTextSection,
            "A non-empty executable .text section was not found.");
    }

    if(programHeaderCount > 0) {
        if(header->e_phoff == 0 || header->e_phentsize < sizeof(Elf64_Phdr)
           || !tableFits(
               fileBytes, header->e_phoff, header->e_phentsize, programHeaderCount)) {
            return fail(
                ElfLoadError::InvalidProgramHeaders,
                "Program header table is invalid.");
        }

        programHeaders_.reserve(static_cast<std::size_t>(programHeaderCount));
        for(std::uint64_t index = 0; index < programHeaderCount; ++index) {
            const auto rawProgramHeader = readObject<Elf64_Phdr>(
                fileBytes, header->e_phoff + index * header->e_phentsize);
            if(!rawProgramHeader
               || !rangeFits(fileBytes, rawProgramHeader->p_offset, rawProgramHeader->p_filesz)
               || rawProgramHeader->p_filesz > rawProgramHeader->p_memsz
               || rawProgramHeader->p_memsz
                      > std::numeric_limits<std::uint64_t>::max()
                            - rawProgramHeader->p_vaddr) {
                return fail(
                    ElfLoadError::InvalidProgramHeaders,
                    "A program header has an invalid file range.");
            }

            programHeaders_.push_back(ProgramHeaderInfo {
                .type = rawProgramHeader->p_type,
                .flags = rawProgramHeader->p_flags,
                .fileOffset = rawProgramHeader->p_offset,
                .virtualAddress = rawProgramHeader->p_vaddr,
                .physicalAddress = rawProgramHeader->p_paddr,
                .fileSize = rawProgramHeader->p_filesz,
                .memorySize = rawProgramHeader->p_memsz,
                .alignment = rawProgramHeader->p_align,
            });
        }
    }

    bool hasStaticSymbolTable = false;
    for(std::size_t sectionIndex = 0; sectionIndex < rawSections.size(); ++sectionIndex) {
        const auto& symbolSection = rawSections[sectionIndex];
        if(symbolSection.sh_type != SHT_SYMTAB && symbolSection.sh_type != SHT_DYNSYM) {
            continue;
        }

        hasStaticSymbolTable = hasStaticSymbolTable || symbolSection.sh_type == SHT_SYMTAB;
        if(symbolSection.sh_entsize < sizeof(Elf64_Sym)
           || symbolSection.sh_size % symbolSection.sh_entsize != 0
           || symbolSection.sh_link >= rawSections.size()) {
            return fail(ElfLoadError::InvalidSymbolTable, "A symbol table is malformed.");
        }

        const auto& stringSection = rawSections[symbolSection.sh_link];
        if(stringSection.sh_type != SHT_STRTAB
           || !rangeFits(fileBytes, stringSection.sh_offset, stringSection.sh_size)) {
            return fail(
                ElfLoadError::InvalidStringTable,
                "A symbol string table is malformed.");
        }

        const auto symbolNames = fileBytes.subspan(
            static_cast<std::size_t>(stringSection.sh_offset),
            static_cast<std::size_t>(stringSection.sh_size));
        const auto symbolCount = symbolSection.sh_size / symbolSection.sh_entsize;

        for(std::uint64_t symbolIndex = 0; symbolIndex < symbolCount; ++symbolIndex) {
            const auto rawSymbol = readObject<Elf64_Sym>(
                fileBytes,
                symbolSection.sh_offset + symbolIndex * symbolSection.sh_entsize);
            if(!rawSymbol) {
                return fail(
                    ElfLoadError::InvalidSymbolTable,
                    "A symbol entry is outside the file.");
            }

            const auto name = readString(symbolNames, rawSymbol->st_name);
            if(!name) {
                return fail(
                    ElfLoadError::InvalidStringTable,
                    "A symbol name is outside its string table.");
            }

            symbols_.push_back(SymbolInfo {
                .name = *name,
                .address = rawSymbol->st_value,
                .size = rawSymbol->st_size,
                .sectionIndex = rawSymbol->st_shndx,
                .binding = ELF64_ST_BIND(rawSymbol->st_info),
                .type = ELF64_ST_TYPE(rawSymbol->st_info),
                .visibility = ELF64_ST_VISIBILITY(rawSymbol->st_other),
                .fromDynamicTable = symbolSection.sh_type == SHT_DYNSYM,
            });
        }
    }

    metadata_.entryPoint = header->e_entry;
    metadata_.fileType = header->e_type;
    metadata_.machine = header->e_machine;
    metadata_.osAbi = header->e_ident[EI_OSABI];
    metadata_.programHeaderCount = programHeaders_.size();
    metadata_.sectionCount = sections_.size();
    metadata_.symbolCount = symbols_.size();
    metadata_.is64Bit = true;
    metadata_.isLittleEndian = true;
    metadata_.isStripped = !hasStaticSymbolTable;

    error_ = ElfLoadError::None;
    errorMessage_.clear();
    valid_ = true;
    return true;
}

} // namespace decompiler
