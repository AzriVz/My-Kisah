#include "Disassembler.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>

static int failures = 0;

static void expect(bool condition, std::string_view message) {
    if(condition) {
        return;
    }
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

int main() {
    using decompiler::InstructionKind;
    using decompiler::OperandKind;

    decompiler::Disassembler disassembler;
    expect(disassembler.isAvailable(), "Capstone should be available");

    constexpr std::uint64_t baseAddress = 0x1000;
    constexpr std::array<std::uint8_t, 12> code {
        0xE8, 0x06, 0x00, 0x00, 0x00, // call 0x100b
        0x75, 0x04,                   // jne 0x100b
        0xEB, 0x02,                   // jmp 0x100b
        0xFF, 0xE0,                   // jmp rax
        0xC3,                         // ret
    };

    const auto result = disassembler.disassemble(code, baseAddress);
    expect(result.succeeded(), "valid x86-64 bytes should disassemble");
    expect(result.instructions.size() == 5, "instruction count mismatch");

    if(result.instructions.size() == 5) {
        const auto& call = result.instructions[0];
        expect(call.address == baseAddress, "call address mismatch");
        expect(call.kind == InstructionKind::Call, "call classification mismatch");
        expect(call.directTarget == 0x100B, "direct call target mismatch");
        expect(call.bytes.size() == 5, "call opcode bytes mismatch");
        expect(call.architectureId != 0, "architecture instruction id is missing");
        expect(
            call.operands.size() == 1
                && call.operands.front().kind == OperandKind::Immediate
                && call.operands.front().immediate == 0x100B,
            "call should expose its immediate operand");

        const auto& conditionalJump = result.instructions[1];
        expect(
            conditionalJump.kind == InstructionKind::ConditionalJump,
            "conditional jump classification mismatch");
        expect(conditionalJump.directTarget == 0x100B, "conditional target mismatch");

        const auto& directJump = result.instructions[2];
        expect(
            directJump.kind == InstructionKind::UnconditionalJump,
            "unconditional jump classification mismatch");
        expect(directJump.directTarget == 0x100B, "unconditional target mismatch");

        expect(
            result.instructions[3].kind == InstructionKind::IndirectJump,
            "indirect jump classification mismatch");
        expect(
            !result.instructions[3].directTarget,
            "indirect jump should not expose a direct target");
        expect(
            result.instructions[3].operands.size() == 1
                && result.instructions[3].operands.front().kind == OperandKind::Register
                && result.instructions[3].operands.front().registerName == "rax",
            "indirect jump should expose its register operand");
        expect(
            !result.instructions[3].registersRead.empty(),
            "indirect jump should report the register it reads");
        expect(
            result.instructions[4].kind == InstructionKind::Return,
            "return classification mismatch");
    }

    constexpr std::array<std::uint8_t, 1> incompleteInstruction {0x0F};
    const auto invalidResult = disassembler.disassemble(incompleteInstruction, 0x3000);
    expect(invalidResult.succeeded(), "unsupported byte should not abort disassembly");
    expect(
        invalidResult.instructions.size() == 1
            && invalidResult.instructions.front().kind == InstructionKind::Invalid,
        "unsupported byte should produce an Invalid instruction");

    const auto emptyResult = disassembler.disassemble({}, baseAddress);
    expect(!emptyResult.succeeded(), "empty byte range should be rejected clearly");

    return failures == 0 ? 0 : 1;
}
