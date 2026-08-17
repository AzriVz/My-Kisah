#include "CallGraph.hpp"

#include <cstdint>
#include <iostream>
#include <string_view>
#include <unordered_map>
#include <vector>

static int failures = 0;

static void expect(bool condition, std::string_view message) {
    if(!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

static decompiler::Instruction directCall(std::uint64_t address, std::uint64_t target) {
    return decompiler::Instruction {
        .address = address,
        .bytes = {0xE8, 0, 0, 0, 0},
        .mnemonic = "call",
        .operandText = {},
        .kind = decompiler::InstructionKind::Call,
        .directTarget = target,
        .architectureId = 0,
        .operands = {},
        .registersRead = {},
        .registersWritten = {},
    };
}

int main() {
    using decompiler::CallGraph;
    using decompiler::FunctionInfo;

    const std::vector<FunctionInfo> functions = {
        {.name = "main", .address = 0x1000, .size = 16},
        {.name = "worker", .address = 0x1100, .size = 16},
    };
    const std::unordered_map<std::uint64_t, std::vector<decompiler::Instruction>> instructions = {
        {0x1000, {directCall(0x1000, 0x1100), directCall(0x1005, 0x1100)}},
        {0x1100, {directCall(0x1100, 0x1100), directCall(0x1105, 0x2000)}},
    };

    CallGraph graph;
    expect(graph.build(functions, instructions), "call graph should build");
    expect(graph.nodes().size() == 3, "internal and external nodes should be present once");
    expect(graph.edges().size() == 3, "duplicate caller/callee edges should be collapsed");
    expect(graph.nodeAt(0x1000) != nullptr, "main node should be addressable");
    expect(graph.nodeAt(0x2000) != nullptr, "external direct target should become a node");
    if(const auto* external = graph.nodeAt(0x2000)) {
        expect(external->isExternal, "unresolved direct target should be external");
    }
    const auto recursiveEdges = graph.outgoingEdges(0x1100);
    expect(recursiveEdges.size() == 2, "recursive and external calls should both be retained");

    graph.clear();
    expect(graph.nodes().empty() && graph.edges().empty(), "clear should reset the graph");
    return failures == 0 ? 0 : 1;
}
