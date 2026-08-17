#include "AbiAnalyzer.hpp"
#include "Disassembler.hpp"
#include "IRLifter.hpp"
#include "Register.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>

static int failures = 0;

static void expect(bool condition, std::string_view message) {
    if(condition) {
        return;
    }
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

template<std::size_t Size>
static decompiler::DisassemblyResult disassemble(
    decompiler::Disassembler& disassembler,
    const std::array<std::uint8_t, Size>& code,
    std::uint64_t address) {
    auto result = disassembler.disassemble(std::span<const std::uint8_t>(code), address);
    expect(result.succeeded(), "test bytes should disassemble");
    return result;
}

static bool hasOpcode(const decompiler::IRFunction& function, decompiler::IROpcode opcode) {
    return std::any_of(
        function.instructions.begin(),
        function.instructions.end(),
        [opcode](const auto& instruction) { return instruction.opcode == opcode; });
}

static void testRegisterNormalization() {
    const auto eax = decompiler::RegisterNormalizer::normalize("eax");
    expect(eax && eax->id == decompiler::RegisterId::Rax, "eax should alias rax");
    expect(eax && eax->bitWidth == 32, "eax should be 32 bits wide");
    expect(eax && eax->zeroExtendsOnWrite, "writing eax should zero-extend rax");

    const auto ah = decompiler::RegisterNormalizer::normalize("AH");
    expect(ah && ah->id == decompiler::RegisterId::Rax, "AH should alias rax");
    expect(ah && ah->bitWidth == 8 && ah->bitOffset == 8, "AH bit range mismatch");

    const auto r9d = decompiler::RegisterNormalizer::normalize("r9d");
    expect(r9d && r9d->id == decompiler::RegisterId::R9, "r9d should alias r9");
    expect(r9d && r9d->zeroExtendsOnWrite, "writing r9d should zero-extend r9");
    expect(
        !decompiler::RegisterNormalizer::normalize("xmm0"),
        "unsupported register families should remain unknown");
}

static void testRegisterParametersAndArithmetic(decompiler::Disassembler& disassembler) {
    constexpr std::array<std::uint8_t, 5> code {
        0x89, 0xF8, // mov eax, edi
        0x01, 0xF0, // add eax, esi
        0xC3,       // ret
    };
    const auto disassembly = disassemble(disassembler, code, 0x1000);
    const decompiler::AbiAnalyzer analyzer;
    const auto analysis = analyzer.analyze(disassembly.instructions);

    expect(analysis.parameters.size() == 2, "two register parameters should be inferred");
    if(analysis.parameters.size() == 2) {
        expect(analysis.parameters[0].name == "arg0", "first parameter name mismatch");
        expect(analysis.parameters[1].name == "arg1", "second parameter name mismatch");
    }
    expect(analysis.returnsValue, "rax write followed by ret should infer a return value");
    expect(analysis.returnBitWidth == 32, "arithmetic return width should be 32 bits");

    const decompiler::IRLifter lifter;
    const auto ir = lifter.lift(
        decompiler::FunctionInfo {
            .name = "add",
            .address = 0x1000,
            .size = code.size(),
            .source = decompiler::FunctionSource::Heuristic,
            .sizeIsEstimated = false,
        },
        disassembly.instructions,
        analysis);
    expect(ir.parameters.size() == 2, "IR should retain inferred parameters");
    expect(hasOpcode(ir, decompiler::IROpcode::Assign), "mov should lift to Assign");
    expect(hasOpcode(ir, decompiler::IROpcode::Add), "add should lift to Add");
    expect(hasOpcode(ir, decompiler::IROpcode::Return), "ret should lift to Return");

    const auto add = std::find_if(ir.instructions.begin(), ir.instructions.end(), [](const auto& value) {
        return value.opcode == decompiler::IROpcode::Add;
    });
    expect(add != ir.instructions.end() && add->operands.size() == 2, "Add operands are missing");
    if(add != ir.instructions.end() && add->operands.size() == 2) {
        expect(
            add->operands[1].kind == decompiler::IRValueKind::Parameter
                && add->operands[1].name == "arg1",
            "Add source should reference arg1");
    }
}

static void testStackValues(decompiler::Disassembler& disassembler) {
    constexpr std::array<std::uint8_t, 10> code {
        0x55,                   // push rbp
        0x48, 0x89, 0xE5,       // mov rbp, rsp
        0x89, 0x7D, 0xFC,       // mov dword ptr [rbp-4], edi
        0x8B, 0x45, 0xFC,       // mov eax, dword ptr [rbp-4]
    };
    constexpr std::array<std::uint8_t, 2> epilogue {0x5D, 0xC3};
    std::array<std::uint8_t, 12> completeCode {};
    std::copy(code.begin(), code.end(), completeCode.begin());
    std::copy(epilogue.begin(), epilogue.end(), completeCode.begin() + code.size());

    const auto disassembly = disassemble(disassembler, completeCode, 0x2000);
    const decompiler::AbiAnalyzer analyzer;
    const auto analysis = analyzer.analyze(disassembly.instructions);
    expect(!analysis.parameters.empty() && analysis.parameters[0].name == "arg0", "arg0 missing");
    expect(analysis.stackVariables.size() == 1, "one stack local should be inferred");
    if(analysis.stackVariables.size() == 1) {
        expect(analysis.stackVariables[0].offset == -4, "stack-local offset mismatch");
        expect(analysis.stackVariables[0].name == "local_4", "stack-local name mismatch");
    }

    const decompiler::IRLifter lifter;
    const auto ir = lifter.lift(
        decompiler::FunctionInfo {
            .name = "stack_local",
            .address = 0x2000,
            .size = completeCode.size(),
            .source = decompiler::FunctionSource::Heuristic,
            .sizeIsEstimated = false,
        },
        disassembly.instructions,
        analysis);
    expect(ir.localVariables.size() == 1, "IR should retain the stack local");
    expect(hasOpcode(ir, decompiler::IROpcode::Store), "stack write should lift to Store");
    expect(hasOpcode(ir, decompiler::IROpcode::Load), "stack read should lift to Load");
}

static void testStackParameterAndCall(decompiler::Disassembler& disassembler) {
    constexpr std::array<std::uint8_t, 7> stackArgumentCode {
        0x55,                   // push rbp
        0x48, 0x89, 0xE5,       // mov rbp, rsp
        0x8B, 0x45, 0x10,       // mov eax, dword ptr [rbp+16]
    };
    constexpr std::array<std::uint8_t, 2> epilogue {0x5D, 0xC3};
    std::array<std::uint8_t, 9> completeCode {};
    std::copy(stackArgumentCode.begin(), stackArgumentCode.end(), completeCode.begin());
    std::copy(epilogue.begin(), epilogue.end(), completeCode.begin() + stackArgumentCode.size());

    const auto stackDisassembly = disassemble(disassembler, completeCode, 0x3000);
    const decompiler::AbiAnalyzer analyzer;
    const auto stackAnalysis = analyzer.analyze(stackDisassembly.instructions);
    const auto stackParameter = std::find_if(
        stackAnalysis.parameters.begin(), stackAnalysis.parameters.end(), [](const auto& parameter) {
            return parameter.stackOffset && *parameter.stackOffset == 16;
        });
    expect(
        stackParameter != stackAnalysis.parameters.end() && stackParameter->name == "arg6",
        "first stack parameter should be arg6");

    constexpr std::array<std::uint8_t, 6> callCode {
        0xE8, 0x00, 0x00, 0x00, 0x00, // call next instruction
        0xC3,                         // ret
    };
    const auto callDisassembly = disassemble(disassembler, callCode, 0x4000);
    const auto callAnalysis = analyzer.analyze(callDisassembly.instructions);
    const decompiler::IRLifter lifter;
    const auto ir = lifter.lift(
        decompiler::FunctionInfo {
            .name = "caller",
            .address = 0x4000,
            .size = callCode.size(),
            .source = decompiler::FunctionSource::Heuristic,
            .sizeIsEstimated = false,
        },
        callDisassembly.instructions,
        callAnalysis);
    expect(hasOpcode(ir, decompiler::IROpcode::Call), "call should lift to Call");
}

static void testUnsupportedInstruction(decompiler::Disassembler& disassembler) {
    constexpr std::array<std::uint8_t, 3> code {
        0xFF, 0xC0, // inc eax
        0xC3,       // ret
    };
    const auto disassembly = disassemble(disassembler, code, 0x5000);
    const decompiler::AbiAnalyzer analyzer;
    const auto analysis = analyzer.analyze(disassembly.instructions);
    const decompiler::IRLifter lifter;
    const auto ir = lifter.lift(
        decompiler::FunctionInfo {
            .name = "unsupported",
            .address = 0x5000,
            .size = code.size(),
            .source = decompiler::FunctionSource::Heuristic,
            .sizeIsEstimated = false,
        },
        disassembly.instructions,
        analysis);
    expect(
        !ir.instructions.empty()
            && ir.instructions.front().opcode == decompiler::IROpcode::Unknown,
        "unsupported instructions should lift to Unknown");
}

int main() {
    testRegisterNormalization();

    decompiler::Disassembler disassembler;
    expect(disassembler.isAvailable(), "Capstone should be available");
    testRegisterParametersAndArithmetic(disassembler);
    testStackValues(disassembler);
    testStackParameterAndCall(disassembler);
    testUnsupportedInstruction(disassembler);
    return failures == 0 ? 0 : 1;
}
