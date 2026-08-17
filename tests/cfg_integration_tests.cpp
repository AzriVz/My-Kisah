#include "AnalysisSession.hpp"
#include "ControlFlowGraph.hpp"
#include "FunctionInfo.hpp"

#include <algorithm>
#include <iostream>

int main(int argc, char* argv[]) {
    if(argc < 2) {
        std::cerr << "Compiler-produced loop sample path is required\n";
        return 1;
    }

    decompiler::AnalysisSession session;
    if(!session.analyze(argv[1])) {
        std::cerr << "Loop sample analysis failed: " << session.errorMessage() << '\n';
        return 1;
    }

    for(const auto& function : session.functions()) {
        const auto* graph = session.controlFlowGraphFor(function.address);
        if(graph == nullptr || !graph->isValid() || graph->entryBlock() == nullptr) {
            std::cerr << "A discovered function does not have a valid cached CFG\n";
            return 1;
        }
        if(graph->depthFirstOrder().empty() || graph->reversePostOrder().empty()) {
            std::cerr << "CFG traversal order is empty\n";
            return 1;
        }
    }

    const auto loopFunction = std::find_if(
        session.functions().begin(), session.functions().end(), [](const auto& function) {
            return function.name.find("sum_to_n") != std::string::npos;
        });
    if(loopFunction == session.functions().end()) {
        std::cerr << "sum_to_n was not discovered\n";
        return 1;
    }

    const auto* loopGraph = session.controlFlowGraphFor(loopFunction->address);
    if(loopGraph == nullptr || loopGraph->blocks().size() < 3) {
        std::cerr << "sum_to_n did not produce enough basic blocks\n";
        return 1;
    }
    if(loopGraph->backEdges().empty() || loopGraph->loopHeaders().empty()) {
        std::cerr << "sum_to_n loop back edge was not detected\n";
        return 1;
    }

    const auto conditionalBlock = std::find_if(
        loopGraph->blocks().begin(), loopGraph->blocks().end(), [](const auto& block) {
            return !block.instructions.empty()
                   && block.instructions.back().kind
                          == decompiler::InstructionKind::ConditionalJump
                   && block.successors.size() == 2;
        });
    if(conditionalBlock == loopGraph->blocks().end()) {
        std::cerr << "sum_to_n has no conditional block with two successors\n";
        return 1;
    }

    return 0;
}

