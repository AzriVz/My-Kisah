#include "AnalysisSession.hpp"
#include "ElfFixture.hpp"

#include <elf.h>

#include <algorithm>
#include <array>
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

static std::vector<std::uint8_t> makeStrippedCallImage() {
    auto image = test_support::makeElf64Image();
    const std::array<std::uint8_t, 7> text {
        0xE8, 0x01, 0x00, 0x00, 0x00, // call 0x400106
        0xC3,                         // ret
        0xC3,                         // direct-call target
    };
    std::copy(text.begin(), text.end(), image.begin() + test_support::textOffset);

    Elf64_Shdr textSection {};
    std::memcpy(
        &textSection,
        image.data() + test_support::sectionHeaderOffset + sizeof(Elf64_Shdr),
        sizeof(textSection));
    textSection.sh_size = text.size();
    test_support::writeObject(
        image,
        test_support::sectionHeaderOffset + sizeof(Elf64_Shdr),
        textSection);

    Elf64_Shdr formerSymbolTable {};
    std::memcpy(
        &formerSymbolTable,
        image.data() + test_support::sectionHeaderOffset + 3 * sizeof(Elf64_Shdr),
        sizeof(formerSymbolTable));
    formerSymbolTable.sh_type = SHT_PROGBITS;
    formerSymbolTable.sh_entsize = 0;
    formerSymbolTable.sh_link = 0;
    formerSymbolTable.sh_info = 0;
    test_support::writeObject(
        image,
        test_support::sectionHeaderOffset + 3 * sizeof(Elf64_Shdr),
        formerSymbolTable);
    return image;
}

static std::vector<std::uint8_t> makeStrippedHeuristicImage() {
    auto image = test_support::makeElf64Image();
    const std::array<std::uint8_t, 14> text {
        0xC3,                         // entry function: ret
        0x90, 0x90, 0x90, 0x90,
        0x90, 0x90, 0x90,             // alignment padding
        0x55,                         // push rbp
        0x48, 0x89, 0xE5,             // mov rbp, rsp
        0x5D,                         // pop rbp
        0xC3,                         // ret
    };
    std::copy(text.begin(), text.end(), image.begin() + test_support::textOffset);

    Elf64_Shdr textSection {};
    std::memcpy(
        &textSection,
        image.data() + test_support::sectionHeaderOffset + sizeof(Elf64_Shdr),
        sizeof(textSection));
    textSection.sh_size = text.size();
    test_support::writeObject(
        image,
        test_support::sectionHeaderOffset + sizeof(Elf64_Shdr),
        textSection);

    Elf64_Shdr formerSymbolTable {};
    std::memcpy(
        &formerSymbolTable,
        image.data() + test_support::sectionHeaderOffset + 3 * sizeof(Elf64_Shdr),
        sizeof(formerSymbolTable));
    formerSymbolTable.sh_type = SHT_PROGBITS;
    formerSymbolTable.sh_entsize = 0;
    formerSymbolTable.sh_link = 0;
    formerSymbolTable.sh_info = 0;
    test_support::writeObject(
        image,
        test_support::sectionHeaderOffset + 3 * sizeof(Elf64_Shdr),
        formerSymbolTable);
    return image;
}

int main(int argc, char* argv[]) {
    if(argc < 2) {
        std::cerr << "Compiler-produced sample path is required\n";
        return 1;
    }

    decompiler::AnalysisSession realSession;
    expect(realSession.analyze(argv[1]), "real compiler-produced ELF should analyze");
    expect(realSession.isValid(), "real analysis session should be valid");

    const auto mainFunction = std::find_if(
        realSession.functions().begin(),
        realSession.functions().end(),
        [](const auto& function) { return function.name == "main"; });
    expect(mainFunction != realSession.functions().end(), "main should be discovered");

    if(mainFunction != realSession.functions().end()) {
        const auto* instructions = realSession.instructionsFor(mainFunction->address);
        expect(instructions != nullptr && !instructions->empty(), "main assembly should be cached");
        if(instructions != nullptr) {
            const auto internalCall = std::find_if(
                instructions->begin(), instructions->end(), [&](const auto& instruction) {
                    return instruction.kind == decompiler::InstructionKind::Call
                           && instruction.directTarget
                           && realSession.functionAt(*instruction.directTarget) != nullptr;
                });
            expect(
                internalCall != instructions->end(),
                "main should contain a detected direct function call");
        }
    }

    test_support::TemporaryElfFiles files;
    const auto strippedPath = files.write("stripped-direct-call", makeStrippedCallImage());
    decompiler::AnalysisSession strippedSession;
    expect(strippedSession.analyze(strippedPath), "stripped direct-call fixture should analyze");
    expect(strippedSession.elfLoader().metadata().isStripped, "fixture should be stripped");
    expect(strippedSession.functions().size() == 2, "entry and direct-call target should be found");

    const auto directCallFunction = std::find_if(
        strippedSession.functions().begin(),
        strippedSession.functions().end(),
        [](const auto& function) {
            return function.source == decompiler::FunctionSource::DirectCallTarget;
        });
    expect(
        directCallFunction != strippedSession.functions().end(),
        "direct-call target source should be recorded");
    if(directCallFunction != strippedSession.functions().end()) {
        expect(
            directCallFunction->name == "sub_400106",
            "direct-call fallback name should use its address");
    }

    const auto heuristicPath = files.write(
        "stripped-heuristic", makeStrippedHeuristicImage());
    decompiler::AnalysisSession heuristicSession;
    expect(
        heuristicSession.analyze(heuristicPath),
        "stripped prologue fixture should analyze");
    const auto heuristicFunction = std::find_if(
        heuristicSession.functions().begin(),
        heuristicSession.functions().end(),
        [](const auto& function) {
            return function.address == test_support::textAddress + 8
                   && function.source == decompiler::FunctionSource::Heuristic;
        });
    expect(
        heuristicFunction != heuristicSession.functions().end(),
        "aligned stripped prologue should become a heuristic function candidate");

    return failures == 0 ? 0 : 1;
}
