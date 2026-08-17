#include "AbiAnalyzer.hpp"
#include "BasicBlockBuilder.hpp"
#include "ControlFlowGraph.hpp"
#include "DataFlowAnalyzer.hpp"
#include "Disassembler.hpp"
#include "IRLifter.hpp"
#include "PseudocodeGenerator.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
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

template<std::size_t Size>
static std::string recover(
    decompiler::Disassembler& disassembler,
    const std::array<std::uint8_t, Size>& code,
    std::uint64_t address,
    std::string name,
    std::span<const decompiler::FunctionPrototype> prototypes = {}) {
    const auto disassembly = disassembler.disassemble(code, address);
    expect(disassembly.succeeded(), "test bytes should disassemble");

    const decompiler::FunctionInfo function {
        .name = std::move(name),
        .address = address,
        .size = code.size(),
        .source = decompiler::FunctionSource::Heuristic,
        .sizeIsEstimated = false,
    };
    const decompiler::AbiAnalyzer abiAnalyzer;
    const auto abiAnalysis = abiAnalyzer.analyze(disassembly.instructions);
    const decompiler::IRLifter irLifter;
    const auto ir = irLifter.lift(function, disassembly.instructions, abiAnalysis);
    const decompiler::BasicBlockBuilder blockBuilder;
    decompiler::ControlFlowGraph controlFlowGraph;
    expect(
        controlFlowGraph.build(blockBuilder.build(disassembly.instructions)),
        "test CFG should build");
    const decompiler::DataFlowAnalyzer dataFlowAnalyzer;
    const auto dataFlow =
        dataFlowAnalyzer.analyze(ir, disassembly.instructions, controlFlowGraph, abiAnalysis);
    const decompiler::PseudocodeGenerator generator;
    return generator.generate(
        function, abiAnalysis, controlFlowGraph, dataFlow, prototypes);
}

static void testArithmetic(decompiler::Disassembler& disassembler) {
    constexpr std::array<std::uint8_t, 5> code {
        0x89, 0xF8, // mov eax, edi
        0x01, 0xF0, // add eax, esi
        0xC3,       // ret
    };
    const auto pseudocode = recover(disassembler, code, 0x1000, "add");
    expect(
        pseudocode.find("int add(int arg0, int arg1)") != std::string::npos,
        "arithmetic signature mismatch");
    expect(
        pseudocode.find("return (arg0 + arg1);") != std::string::npos,
        "register operations should fold into an arithmetic return expression");
    expect(
        pseudocode.find("eax =") == std::string::npos,
        "physical register assignments should not leak into simple pseudocode");
}

static void testCall(decompiler::Disassembler& disassembler) {
    constexpr std::array<std::uint8_t, 16> code {
        0xBE, 0x03, 0x00, 0x00, 0x00, // mov esi, 3
        0xBF, 0x02, 0x00, 0x00, 0x00, // mov edi, 2
        0xE8, 0xF1, 0x0F, 0x00, 0x00, // call 0x3000
        0xC3,                         // ret
    };
    const std::array prototypes {
        decompiler::FunctionPrototype {
            .address = 0x3000,
            .name = "add",
            .parameterCount = 2,
            .returnsValue = true,
            .returnType = decompiler::ValueType::Integer,
            .returnBitWidth = 32,
        },
    };
    const auto pseudocode = recover(disassembler, code, 0x2000, "caller", prototypes);
    expect(
        pseudocode.find("result = add(2, 3);") != std::string::npos,
        "call arguments or callee name were not recovered");
    expect(
        pseudocode.find("return result;") != std::string::npos,
        "call result should flow into return");
}

static void testIfElse(decompiler::Disassembler& disassembler) {
    constexpr std::array<std::uint8_t, 17> code {
        0x83, 0xFF, 0x00,             // cmp edi, 0
        0x74, 0x06,                   // je 0x100b
        0xB8, 0x01, 0x00, 0x00, 0x00, // mov eax, 1
        0xC3,                         // ret
        0xB8, 0x02, 0x00, 0x00, 0x00, // mov eax, 2
        0xC3,                         // ret
    };
    const auto pseudocode = recover(disassembler, code, 0x1000, "choose");
    expect(pseudocode.find("if (") != std::string::npos, "if was not reconstructed");
    expect(pseudocode.find("} else {") != std::string::npos, "else was not reconstructed");
    expect(
        pseudocode.find("arg0 == 0") != std::string::npos,
        "conditional comparison was not recovered");
    expect(
        pseudocode.find("return result;") != std::string::npos,
        "both branches should contain recovered returns");
}

static void testUnsupportedInstruction(decompiler::Disassembler& disassembler) {
    constexpr std::array<std::uint8_t, 3> code {
        0xFF, 0xC0, // inc eax
        0xC3,       // ret
    };
    const auto pseudocode = recover(disassembler, code, 0x4000, "unsupported");
    expect(
        pseudocode.find("Unsupported instruction at 0x4000: inc eax")
            != std::string::npos,
        "unsupported instructions should produce an address-aware comment");
}

int main() {
    decompiler::Disassembler disassembler;
    expect(disassembler.isAvailable(), "Capstone should be available");
    testArithmetic(disassembler);
    testCall(disassembler);
    testIfElse(disassembler);
    testUnsupportedInstruction(disassembler);
    return failures == 0 ? 0 : 1;
}
