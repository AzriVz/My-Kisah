#include "Disassembler.hpp"

#include <capstone/capstone.h>
#include <capstone/x86.h>

#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>

namespace decompiler {

static void freeCapstoneInstruction(cs_insn* instruction) noexcept {
    if(instruction != nullptr) {
        cs_free(instruction, 1);
    }
}

static std::optional<std::uint64_t> directTarget(const cs_insn& instruction) noexcept {
    if(instruction.detail == nullptr || instruction.detail->x86.op_count == 0) {
        return std::nullopt;
    }

    const auto& operand = instruction.detail->x86.operands[0];
    if(operand.type != X86_OP_IMM) {
        return std::nullopt;
    }

    return static_cast<std::uint64_t>(operand.imm);
}

static InstructionKind classifyInstruction(
    csh handle,
    const cs_insn& instruction,
    const std::optional<std::uint64_t>& target) noexcept {
    if(cs_insn_group(handle, &instruction, CS_GRP_CALL)) {
        return InstructionKind::Call;
    }

    if(cs_insn_group(handle, &instruction, CS_GRP_RET)) {
        return InstructionKind::Return;
    }

    if(!cs_insn_group(handle, &instruction, CS_GRP_JUMP)) {
        return InstructionKind::Normal;
    }

    if(instruction.id == X86_INS_JMP || instruction.id == X86_INS_LJMP) {
        return target ? InstructionKind::UnconditionalJump : InstructionKind::IndirectJump;
    }

    return InstructionKind::ConditionalJump;
}

static Instruction copyInstruction(csh handle, const cs_insn& source) {
    Instruction instruction;
    instruction.address = source.address;
    instruction.bytes.assign(source.bytes, source.bytes + source.size);
    instruction.mnemonic = source.mnemonic;
    instruction.operandText = source.op_str;
    instruction.directTarget = directTarget(source);
    instruction.kind = classifyInstruction(handle, source, instruction.directTarget);
    return instruction;
}

static Instruction invalidInstruction(std::uint64_t address, std::uint8_t byte) {
    std::ostringstream operand;
    operand << "0x" << std::hex << std::setfill('0') << std::setw(2)
            << static_cast<unsigned int>(byte);

    return Instruction {
        .address = address,
        .bytes = {byte},
        .mnemonic = "invalid",
        .operandText = operand.str(),
        .kind = InstructionKind::Invalid,
        .directTarget = std::nullopt,
    };
}

Disassembler::Disassembler() {
    csh capstoneHandle = 0;
    const auto openResult = cs_open(CS_ARCH_X86, CS_MODE_64, &capstoneHandle);
    if(openResult != CS_ERR_OK) {
        errorMessage_ = cs_strerror(openResult);
        return;
    }

    const auto detailResult = cs_option(capstoneHandle, CS_OPT_DETAIL, CS_OPT_ON);
    if(detailResult != CS_ERR_OK) {
        errorMessage_ = cs_strerror(detailResult);
        cs_close(&capstoneHandle);
        return;
    }

    handle_ = capstoneHandle;
}

Disassembler::~Disassembler() {
    if(handle_ != 0) {
        auto capstoneHandle = static_cast<csh>(handle_);
        cs_close(&capstoneHandle);
        handle_ = 0;
    }
}

bool Disassembler::isAvailable() const noexcept {
    return handle_ != 0;
}

std::string_view Disassembler::errorMessage() const noexcept {
    return errorMessage_;
}

DisassemblyResult
Disassembler::disassemble(std::span<const std::uint8_t> code, std::uint64_t address) const {
    DisassemblyResult result;
    if(!isAvailable()) {
        result.errorMessage = errorMessage_.empty() ? "Capstone is unavailable." : errorMessage_;
        return result;
    }

    if(code.empty()) {
        result.errorMessage = "No instruction bytes were provided.";
        return result;
    }

    const auto handle = static_cast<csh>(handle_);
    std::unique_ptr<cs_insn, decltype(&freeCapstoneInstruction)> instruction(
        cs_malloc(handle), &freeCapstoneInstruction);
    if(!instruction) {
        result.errorMessage = "Capstone could not allocate an instruction object.";
        return result;
    }

    const auto* nextByte = code.data();
    auto remainingSize = code.size();
    auto currentAddress = address;
    result.instructions.reserve(code.size() / 2 + 1);

    while(remainingSize > 0) {
        if(cs_disasm_iter(
               handle,
               &nextByte,
               &remainingSize,
               &currentAddress,
               instruction.get())) {
            result.instructions.push_back(copyInstruction(handle, *instruction));
            continue;
        }

        const auto capstoneError = cs_errno(handle);
        if(capstoneError != CS_ERR_OK) {
            result.errorMessage = cs_strerror(capstoneError);
            return result;
        }

        result.instructions.push_back(invalidInstruction(currentAddress, *nextByte));
        ++nextByte;
        --remainingSize;
        if(currentAddress == std::numeric_limits<std::uint64_t>::max()) {
            if(remainingSize > 0) {
                result.errorMessage = "Instruction address range overflows 64 bits.";
            }
            return result;
        }
        ++currentAddress;
    }

    return result;
}

} // namespace decompiler
