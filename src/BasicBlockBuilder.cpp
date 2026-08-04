#include "BasicBlockBuilder.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <unordered_set>
#include <utility>

namespace decompiler {

static bool terminatesBasicBlock(InstructionKind kind) noexcept {
    return kind == InstructionKind::Return || kind == InstructionKind::ConditionalJump
           || kind == InstructionKind::UnconditionalJump
           || kind == InstructionKind::IndirectJump;
}

static std::uint64_t instructionEnd(const Instruction& instruction) noexcept {
    const auto byteCount = std::max<std::uint64_t>(instruction.bytes.size(), 1);
    if(instruction.address > std::numeric_limits<std::uint64_t>::max() - byteCount) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return instruction.address + byteCount;
}

static void finishBlock(BasicBlock& current, std::vector<BasicBlock>& blocks) {
    if(current.instructions.empty()) {
        return;
    }

    current.startAddress = current.instructions.front().address;
    current.endAddress = instructionEnd(current.instructions.back());
    blocks.push_back(std::move(current));
    current = {};
}

std::vector<BasicBlock>
BasicBlockBuilder::build(std::span<const Instruction> instructions) const {
    if(instructions.empty()) {
        return {};
    }

    std::unordered_set<std::uint64_t> instructionAddresses;
    instructionAddresses.reserve(instructions.size());
    for(const auto& instruction : instructions) {
        instructionAddresses.insert(instruction.address);
    }

    std::unordered_set<std::uint64_t> leaders;
    leaders.reserve(instructions.size());
    leaders.insert(instructions.front().address);

    for(std::size_t index = 0; index < instructions.size(); ++index) {
        const auto& instruction = instructions[index];
        const bool isDirectBranch = instruction.kind == InstructionKind::ConditionalJump
                                    || instruction.kind == InstructionKind::UnconditionalJump;
        if(isDirectBranch && instruction.directTarget
           && instructionAddresses.contains(*instruction.directTarget)) {
            leaders.insert(*instruction.directTarget);
        }

        if(terminatesBasicBlock(instruction.kind) && index + 1 < instructions.size()) {
            // A block after ret/indirect jump is represented, but CFG reachability
            // decides whether it is actually executable.
            leaders.insert(instructions[index + 1].address);
        }
    }

    std::vector<BasicBlock> blocks;
    blocks.reserve(leaders.size());
    BasicBlock current;

    for(const auto& instruction : instructions) {
        if(!current.instructions.empty() && leaders.contains(instruction.address)) {
            finishBlock(current, blocks);
        }
        current.instructions.push_back(instruction);
    }
    finishBlock(current, blocks);

    return blocks;
}

} // namespace decompiler
