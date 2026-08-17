#include "BasicBlockBuilder.hpp"
#include "ControlFlowGraph.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
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

static decompiler::Instruction instruction(
    std::uint64_t address,
    decompiler::InstructionKind kind,
    std::optional<std::uint64_t> target = std::nullopt,
    std::size_t byteCount = 2) {
    return decompiler::Instruction {
        .address = address,
        .bytes = std::vector<std::uint8_t>(byteCount, 0x90),
        .mnemonic = "test",
        .operandText = {},
        .kind = kind,
        .directTarget = target,
        .architectureId = 0,
        .operands = {},
        .registersRead = {},
        .registersWritten = {},
    };
}

static bool containsAddress(
    const std::vector<std::uint64_t>& addresses,
    std::uint64_t address) {
    return std::find(addresses.begin(), addresses.end(), address) != addresses.end();
}

int main() {
    using decompiler::InstructionKind;

    const decompiler::BasicBlockBuilder builder;
    const std::vector<decompiler::Instruction> conditionalInstructions {
        instruction(0x1000, InstructionKind::Normal),
        instruction(0x1002, InstructionKind::ConditionalJump, 0x1008),
        instruction(0x1004, InstructionKind::Normal),
        instruction(0x1006, InstructionKind::UnconditionalJump, 0x100A),
        instruction(0x1008, InstructionKind::Normal),
        instruction(0x100A, InstructionKind::Return, std::nullopt, 1),
    };

    auto conditionalBlocks = builder.build(conditionalInstructions);
    expect(conditionalBlocks.size() == 4, "conditional function should have four leaders");
    if(conditionalBlocks.size() == 4) {
        expect(conditionalBlocks[0].startAddress == 0x1000, "entry leader mismatch");
        expect(conditionalBlocks[1].startAddress == 0x1004, "fallthrough leader mismatch");
        expect(conditionalBlocks[2].startAddress == 0x1008, "branch target leader mismatch");
        expect(conditionalBlocks[3].startAddress == 0x100A, "join leader mismatch");
        expect(
            conditionalBlocks[0].instructions.size() == 2,
            "branch should terminate the entry block");
    }

    decompiler::ControlFlowGraph conditionalGraph;
    expect(
        conditionalGraph.build(std::move(conditionalBlocks)),
        "conditional CFG should build");
    const auto* conditionalEntry = conditionalGraph.blockAt(0x1000);
    expect(conditionalEntry != nullptr, "conditional entry block missing");
    if(conditionalEntry != nullptr) {
        expect(conditionalEntry->successors.size() == 2, "conditional block needs two edges");
        expect(
            containsAddress(conditionalEntry->successors, 0x1004),
            "conditional fallthrough edge missing");
        expect(
            containsAddress(conditionalEntry->successors, 0x1008),
            "conditional target edge missing");
    }

    const auto* directJumpBlock = conditionalGraph.blockAt(0x1004);
    expect(
        directJumpBlock != nullptr && directJumpBlock->successors.size() == 1
            && directJumpBlock->successors.front() == 0x100A,
        "unconditional jump should have exactly one edge");

    const auto* returnBlock = conditionalGraph.blockAt(0x100A);
    expect(
        returnBlock != nullptr && returnBlock->successors.empty() && returnBlock->isExit,
        "return block should be an exit without successors");
    if(returnBlock != nullptr) {
        expect(returnBlock->predecessors.size() == 2, "join predecessors mismatch");
    }
    expect(conditionalGraph.unreachableBlocks().empty(), "all conditional blocks should be reachable");
    expect(conditionalGraph.depthFirstOrder().size() == 4, "DFS should visit four blocks");
    expect(conditionalGraph.reversePostOrder().size() == 4, "RPO should visit four blocks");
    expect(conditionalGraph.dominates(0x1000, 0x100A), "entry should dominate the join");
    expect(conditionalGraph.backEdges().empty(), "acyclic CFG should not have back edges");

    const std::vector<decompiler::Instruction> loopInstructions {
        instruction(0x2000, InstructionKind::Normal),
        instruction(0x2002, InstructionKind::ConditionalJump, 0x2008),
        instruction(0x2004, InstructionKind::Normal),
        instruction(0x2006, InstructionKind::UnconditionalJump, 0x2000),
        instruction(0x2008, InstructionKind::Return, std::nullopt, 1),
    };

    decompiler::ControlFlowGraph loopGraph;
    expect(loopGraph.build(builder.build(loopInstructions)), "loop CFG should build");
    expect(loopGraph.blocks().size() == 3, "loop should contain three blocks");
    expect(loopGraph.backEdges().size() == 1, "loop should contain one back edge");
    if(!loopGraph.backEdges().empty()) {
        expect(
            loopGraph.backEdges().front() == std::pair<std::uint64_t, std::uint64_t>(0x2004, 0x2000),
            "loop back edge mismatch");
    }
    expect(
        loopGraph.loopHeaders().size() == 1 && loopGraph.loopHeaders().front() == 0x2000,
        "loop header mismatch");
    expect(loopGraph.dominates(0x2000, 0x2004), "loop header should dominate its body");

    const std::vector<decompiler::Instruction> unreachableInstructions {
        instruction(0x3000, InstructionKind::Return, std::nullopt, 1),
        instruction(0x3001, InstructionKind::Normal, std::nullopt, 1),
        instruction(0x3002, InstructionKind::Return, std::nullopt, 1),
    };
    decompiler::ControlFlowGraph unreachableGraph;
    expect(
        unreachableGraph.build(builder.build(unreachableInstructions)),
        "unreachable CFG should build");
    expect(unreachableGraph.blocks().size() == 2, "instruction after ret should start a block");
    expect(
        unreachableGraph.unreachableBlocks().size() == 1
            && unreachableGraph.unreachableBlocks().front() == 0x3001,
        "block after ret should be unreachable");

    const std::vector<decompiler::Instruction> indirectInstructions {
        instruction(0x4000, InstructionKind::IndirectJump),
        instruction(0x4002, InstructionKind::Return, std::nullopt, 1),
    };
    decompiler::ControlFlowGraph indirectGraph;
    expect(indirectGraph.build(builder.build(indirectInstructions)), "indirect CFG should build");
    const auto* indirectEntry = indirectGraph.entryBlock();
    expect(
        indirectEntry != nullptr && indirectEntry->successors.empty()
            && indirectEntry->hasUnresolvedSuccessor && !indirectEntry->isExit,
        "indirect jump should be marked unresolved, not a known exit");

    decompiler::ControlFlowGraph emptyGraph;
    expect(!emptyGraph.build({}), "empty CFG should fail clearly");

    return failures == 0 ? 0 : 1;
}
