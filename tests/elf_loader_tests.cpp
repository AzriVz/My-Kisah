#include "ElfFixture.hpp"
#include "ElfLoader.hpp"

#include <elf.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string_view>
#include <vector>

static int failures = 0;

static void expect(bool condition, std::string_view message) {
    if(condition) {
        return;
    }

    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

static void replaceHeader(std::vector<std::uint8_t>& image, const Elf64_Ehdr& header) {
    std::memcpy(image.data(), &header, sizeof(header));
}

int main(int argc, char* argv[]) {
    using decompiler::ElfLoadError;
    using decompiler::ElfLoader;

    test_support::TemporaryElfFiles files;
    const auto validImage = test_support::makeElf64Image();
    const auto validPath = files.write("valid.elf", validImage);

    ElfLoader loader;
    expect(loader.load(validPath), "valid ELF64 fixture should load");
    expect(loader.isValid(), "loader should report valid state");
    expect(loader.metadata().is64Bit, "ELF class should be 64-bit");
    expect(loader.metadata().isLittleEndian, "endianness should be little endian");
    expect(loader.metadata().entryPoint == test_support::textAddress, "entry point mismatch");
    expect(loader.metadata().sectionCount == test_support::sectionCount, "section count mismatch");
    expect(!loader.metadata().isStripped, "fixture with .symtab should not be stripped");

    const auto text = loader.findSection(".text");
    expect(text.has_value(), ".text section should be found");
    if(text) {
        expect(text->fileOffset == test_support::textOffset, ".text file offset mismatch");
        expect(text->address == test_support::textAddress, ".text address mismatch");
    }
    expect(!loader.findSection(".missing"), "missing section lookup should be empty");

    const auto textBytes = loader.bytesForSection(".text");
    expect(textBytes.size() == 1 && textBytes.front() == 0xC3, ".text bytes mismatch");
    expect(loader.bytesForSection(".missing").empty(), "missing section bytes should be empty");

    const auto mappedOffset = loader.virtualAddressToFileOffset(test_support::textAddress);
    expect(mappedOffset == test_support::textOffset, "virtual address mapping mismatch");
    const auto mappedAddress = loader.fileOffsetToVirtualAddress(test_support::textOffset);
    expect(mappedAddress == test_support::textAddress, "file offset mapping mismatch");
    expect(
        !loader.virtualAddressToFileOffset(0x900000),
        "unmapped virtual address should be rejected");

    const auto fixtureSymbol = std::find_if(
        loader.symbols().begin(), loader.symbols().end(), [](const auto& symbol) {
            return symbol.name == "fixture_function";
        });
    expect(fixtureSymbol != loader.symbols().end(), "function symbol should be parsed");
    if(fixtureSymbol != loader.symbols().end()) {
        expect(fixtureSymbol->type == STT_FUNC, "symbol type should be STT_FUNC");
        expect(fixtureSymbol->address == test_support::textAddress, "symbol address mismatch");
    }

    expect(
        !loader.load(files.directory() / "does-not-exist"),
        "missing file should be rejected");
    expect(loader.error() == ElfLoadError::FileNotFound, "missing file error mismatch");

    const auto emptyPath = files.write("empty", {});
    expect(!loader.load(emptyPath), "empty file should be rejected");
    expect(loader.error() == ElfLoadError::EmptyFile, "empty file error mismatch");

    auto invalidMagic = validImage;
    invalidMagic[EI_MAG0] = 0;
    expect(!loader.load(files.write("invalid-magic", invalidMagic)), "invalid magic should fail");
    expect(loader.error() == ElfLoadError::InvalidMagic, "invalid magic error mismatch");

    const std::vector<std::uint8_t> truncated(validImage.begin(), validImage.begin() + 32);
    expect(!loader.load(files.write("truncated", truncated)), "truncated header should fail");
    expect(loader.error() == ElfLoadError::TruncatedHeader, "truncated header error mismatch");

    auto elf32 = validImage;
    elf32[EI_CLASS] = ELFCLASS32;
    expect(!loader.load(files.write("elf32", elf32)), "ELF32 should fail");
    expect(loader.error() == ElfLoadError::UnsupportedClass, "ELF class error mismatch");

    auto bigEndian = validImage;
    bigEndian[EI_DATA] = ELFDATA2MSB;
    expect(!loader.load(files.write("big-endian", bigEndian)), "big-endian ELF should fail");
    expect(
        loader.error() == ElfLoadError::UnsupportedEndianness,
        "endianness error mismatch");

    auto wrongMachine = validImage;
    auto header = test_support::readHeader(wrongMachine);
    header.e_machine = EM_386;
    replaceHeader(wrongMachine, header);
    expect(!loader.load(files.write("wrong-machine", wrongMachine)), "x86 ELF should fail");
    expect(
        loader.error() == ElfLoadError::UnsupportedMachine,
        "machine type error mismatch");

    auto pie = validImage;
    header = test_support::readHeader(pie);
    header.e_type = ET_DYN;
    replaceHeader(pie, header);
    expect(
        !loader.load(files.write("shared-object", pie)),
        "ET_DYN input without an interpreter should be rejected as a shared object");
    expect(loader.error() == ElfLoadError::UnsupportedType, "shared-object error mismatch");

    auto pieExecutable = validImage;
    header = test_support::readHeader(pieExecutable);
    header.e_type = ET_DYN;
    replaceHeader(pieExecutable, header);
    Elf64_Phdr interpreter {};
    interpreter.p_type = PT_INTERP;
    interpreter.p_flags = PF_R;
    interpreter.p_offset = test_support::textOffset;
    interpreter.p_vaddr = test_support::textAddress;
    interpreter.p_paddr = test_support::textAddress;
    interpreter.p_filesz = 1;
    interpreter.p_memsz = 1;
    interpreter.p_align = 1;
    test_support::writeObject(pieExecutable, sizeof(Elf64_Ehdr), interpreter);
    expect(
        loader.load(files.write("pie-executable", pieExecutable)),
        "ET_DYN executable with an interpreter should load as PIE");
    expect(
        loader.metadata().isPositionIndependent,
        "PIE metadata should identify position-independent executable");

    auto extendedCounts = validImage;
    header = test_support::readHeader(extendedCounts);
    header.e_shnum = 0;
    header.e_shstrndx = SHN_XINDEX;
    header.e_phnum = PN_XNUM;
    replaceHeader(extendedCounts, header);
    Elf64_Shdr sectionZero {};
    sectionZero.sh_size = test_support::sectionCount;
    sectionZero.sh_link = 2;
    sectionZero.sh_info = 1;
    test_support::writeObject(
        extendedCounts, test_support::sectionHeaderOffset, sectionZero);
    expect(
        loader.load(files.write("extended-counts", extendedCounts)),
        "extended ELF header counts should load");
    expect(
        loader.metadata().sectionCount == test_support::sectionCount,
        "extended section count mismatch");

    auto invalidProgramRange = validImage;
    Elf64_Phdr programHeader {};
    std::memcpy(
        &programHeader,
        invalidProgramRange.data() + sizeof(Elf64_Ehdr),
        sizeof(programHeader));
    programHeader.p_offset = invalidProgramRange.size();
    programHeader.p_filesz = 1;
    programHeader.p_memsz = 1;
    test_support::writeObject(invalidProgramRange, sizeof(Elf64_Ehdr), programHeader);
    expect(
        !loader.load(files.write("invalid-program-range", invalidProgramRange)),
        "out-of-range segment should fail");
    expect(
        loader.error() == ElfLoadError::InvalidProgramHeaders,
        "program-header range error mismatch");

    auto invalidSymbolName = validImage;
    Elf64_Sym symbol {};
    constexpr auto functionSymbolOffset = 0x160 + sizeof(Elf64_Sym);
    std::memcpy(
        &symbol,
        invalidSymbolName.data() + functionSymbolOffset,
        sizeof(symbol));
    symbol.st_name = 0xFFFF;
    test_support::writeObject(invalidSymbolName, functionSymbolOffset, symbol);
    expect(
        !loader.load(files.write("invalid-symbol-name", invalidSymbolName)),
        "out-of-range symbol name should fail");
    expect(
        loader.error() == ElfLoadError::InvalidStringTable,
        "symbol string-table error mismatch");

    auto missingText = validImage;
    Elf64_Shdr textHeader {};
    std::memcpy(
        &textHeader,
        missingText.data() + test_support::sectionHeaderOffset + sizeof(Elf64_Shdr),
        sizeof(textHeader));
    textHeader.sh_name = 0;
    test_support::writeObject(
        missingText,
        test_support::sectionHeaderOffset + sizeof(Elf64_Shdr),
        textHeader);
    expect(
        !loader.load(files.write("missing-text", missingText)),
        "ELF without .text should fail");
    expect(
        loader.error() == ElfLoadError::MissingTextSection,
        "missing .text error mismatch");
    expect(!loader.isValid() && loader.sections().empty(), "failed load should clear parsed state");

    if(argc > 1) {
        expect(loader.load(argv[1]), "compiler-produced non-PIE ELF should load");
        if(loader.isValid()) {
            expect(loader.findSection(".text").has_value(), "real ELF should contain .text");
            expect(!loader.bytesForSection(".text").empty(), "real .text should contain bytes");
            const auto mainSymbol = std::find_if(
                loader.symbols().begin(), loader.symbols().end(), [](const auto& symbol) {
                    return symbol.name == "main" && symbol.type == STT_FUNC;
                });
            expect(mainSymbol != loader.symbols().end(), "real ELF main symbol should be parsed");
        }
    }

    return failures == 0 ? 0 : 1;
}
