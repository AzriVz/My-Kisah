#pragma once

#include <elf.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <vector>

namespace test_support {

inline constexpr std::uint64_t textOffset = 0x100;
inline constexpr std::uint64_t textAddress = 0x400100;
inline constexpr std::uint64_t sectionHeaderOffset = 0x200;
inline constexpr std::size_t sectionCount = 5;

template<typename T>
void writeObject(std::vector<std::uint8_t>& image, std::size_t offset, const T& value) {
    if(offset > image.size() || sizeof(T) > image.size() - offset) {
        throw std::runtime_error("Test fixture object is outside its image");
    }
    std::memcpy(image.data() + offset, &value, sizeof(T));
}

inline void writeText(
    std::vector<std::uint8_t>& image,
    std::size_t offset,
    std::string_view text) {
    if(offset > image.size() || text.size() > image.size() - offset) {
        throw std::runtime_error("Test fixture string is outside its image");
    }
    std::memcpy(image.data() + offset, text.data(), text.size());
}

inline std::vector<std::uint8_t> makeElf64Image() {
    constexpr std::size_t imageSize =
        sectionHeaderOffset + sectionCount * sizeof(Elf64_Shdr);
    constexpr std::uint64_t sectionNameOffset = 0x110;
    constexpr std::uint64_t stringTableOffset = 0x140;
    constexpr std::uint64_t symbolTableOffset = 0x160;
    constexpr std::string_view sectionNames(
        "\0.text\0.shstrtab\0.symtab\0.strtab\0", 33);
    constexpr std::string_view symbolNames("\0fixture_function\0", 18);

    std::vector<std::uint8_t> image(imageSize, 0);

    Elf64_Ehdr header {};
    header.e_ident[EI_MAG0] = ELFMAG0;
    header.e_ident[EI_MAG1] = ELFMAG1;
    header.e_ident[EI_MAG2] = ELFMAG2;
    header.e_ident[EI_MAG3] = ELFMAG3;
    header.e_ident[EI_CLASS] = ELFCLASS64;
    header.e_ident[EI_DATA] = ELFDATA2LSB;
    header.e_ident[EI_VERSION] = EV_CURRENT;
    header.e_ident[EI_OSABI] = ELFOSABI_SYSV;
    header.e_type = ET_EXEC;
    header.e_machine = EM_X86_64;
    header.e_version = EV_CURRENT;
    header.e_entry = textAddress;
    header.e_phoff = sizeof(Elf64_Ehdr);
    header.e_shoff = sectionHeaderOffset;
    header.e_ehsize = sizeof(Elf64_Ehdr);
    header.e_phentsize = sizeof(Elf64_Phdr);
    header.e_phnum = 1;
    header.e_shentsize = sizeof(Elf64_Shdr);
    header.e_shnum = sectionCount;
    header.e_shstrndx = 2;
    writeObject(image, 0, header);

    Elf64_Phdr loadSegment {};
    loadSegment.p_type = PT_LOAD;
    loadSegment.p_flags = PF_R | PF_X;
    loadSegment.p_offset = 0;
    loadSegment.p_vaddr = 0x400000;
    loadSegment.p_paddr = 0x400000;
    loadSegment.p_filesz = image.size();
    loadSegment.p_memsz = image.size();
    loadSegment.p_align = 0x1000;
    writeObject(image, sizeof(Elf64_Ehdr), loadSegment);

    image[textOffset] = 0xC3; // ret
    writeText(image, sectionNameOffset, sectionNames);
    writeText(image, stringTableOffset, symbolNames);

    Elf64_Sym functionSymbol {};
    functionSymbol.st_name = 1;
    functionSymbol.st_info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC);
    functionSymbol.st_other = STV_DEFAULT;
    functionSymbol.st_shndx = 1;
    functionSymbol.st_value = textAddress;
    functionSymbol.st_size = 1;
    writeObject(image, symbolTableOffset + sizeof(Elf64_Sym), functionSymbol);

    Elf64_Shdr textSection {};
    textSection.sh_name = 1;
    textSection.sh_type = SHT_PROGBITS;
    textSection.sh_flags = SHF_ALLOC | SHF_EXECINSTR;
    textSection.sh_addr = textAddress;
    textSection.sh_offset = textOffset;
    textSection.sh_size = 1;
    textSection.sh_addralign = 16;
    writeObject(image, sectionHeaderOffset + sizeof(Elf64_Shdr), textSection);

    Elf64_Shdr sectionNameTable {};
    sectionNameTable.sh_name = 7;
    sectionNameTable.sh_type = SHT_STRTAB;
    sectionNameTable.sh_offset = sectionNameOffset;
    sectionNameTable.sh_size = sectionNames.size();
    sectionNameTable.sh_addralign = 1;
    writeObject(image, sectionHeaderOffset + 2 * sizeof(Elf64_Shdr), sectionNameTable);

    Elf64_Shdr symbolTable {};
    symbolTable.sh_name = 17;
    symbolTable.sh_type = SHT_SYMTAB;
    symbolTable.sh_offset = symbolTableOffset;
    symbolTable.sh_size = 2 * sizeof(Elf64_Sym);
    symbolTable.sh_link = 4;
    symbolTable.sh_info = 1;
    symbolTable.sh_addralign = 8;
    symbolTable.sh_entsize = sizeof(Elf64_Sym);
    writeObject(image, sectionHeaderOffset + 3 * sizeof(Elf64_Shdr), symbolTable);

    Elf64_Shdr stringTable {};
    stringTable.sh_name = 25;
    stringTable.sh_type = SHT_STRTAB;
    stringTable.sh_offset = stringTableOffset;
    stringTable.sh_size = symbolNames.size();
    stringTable.sh_addralign = 1;
    writeObject(image, sectionHeaderOffset + 4 * sizeof(Elf64_Shdr), stringTable);

    return image;
}

inline Elf64_Ehdr readHeader(const std::vector<std::uint8_t>& image) {
    Elf64_Ehdr header {};
    std::memcpy(&header, image.data(), sizeof(header));
    return header;
}

class TemporaryElfFiles final {
public:
    TemporaryElfFiles() {
        const auto uniqueValue = std::chrono::steady_clock::now().time_since_epoch().count();
        directory_ = std::filesystem::temp_directory_path()
                     / ("decompiler-elf-tests-" + std::to_string(uniqueValue));
        std::filesystem::create_directories(directory_);
    }

    ~TemporaryElfFiles() {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }

    TemporaryElfFiles(const TemporaryElfFiles&) = delete;
    TemporaryElfFiles& operator=(const TemporaryElfFiles&) = delete;

    std::filesystem::path write(
        std::string_view fileName,
        const std::vector<std::uint8_t>& image) const {
        const auto path = directory_ / fileName;
        std::ofstream output(path, std::ios::binary);
        output.write(
            reinterpret_cast<const char*>(image.data()),
            static_cast<std::streamsize>(image.size()));
        if(!output) {
            throw std::runtime_error("Could not write temporary ELF fixture");
        }
        return path;
    }

    [[nodiscard]] const std::filesystem::path& directory() const noexcept {
        return directory_;
    }

private:
    std::filesystem::path directory_;
};

} // namespace test_support

