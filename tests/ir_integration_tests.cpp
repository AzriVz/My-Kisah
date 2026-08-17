#include "AnalysisSession.hpp"
#include "IR.hpp"

#include <algorithm>
#include <iostream>
#include <string>

static bool hasOpcode(const decompiler::IRFunction& function, decompiler::IROpcode opcode) {
    return std::any_of(
        function.instructions.begin(),
        function.instructions.end(),
        [opcode](const auto& instruction) { return instruction.opcode == opcode; });
}

static const decompiler::FunctionInfo* findFunction(
    const decompiler::AnalysisSession& session,
    const std::string& namePart) {
    const auto function = std::find_if(
        session.functions().begin(), session.functions().end(), [&](const auto& candidate) {
            return candidate.name.find(namePart) != std::string::npos;
        });
    return function == session.functions().end() ? nullptr : &*function;
}

int main(int argc, char* argv[]) {
    if(argc < 3) {
        std::cerr << "Arithmetic and stack-local sample paths are required\n";
        return 1;
    }

    decompiler::AnalysisSession arithmeticSession;
    if(!arithmeticSession.analyze(argv[1])) {
        std::cerr << "Arithmetic analysis failed: " << arithmeticSession.errorMessage() << '\n';
        return 1;
    }

    for(const auto& function : arithmeticSession.functions()) {
        const auto* analysis = arithmeticSession.abiAnalysisFor(function.address);
        const auto* ir = arithmeticSession.irFor(function.address);
        if(analysis == nullptr || ir == nullptr || ir->instructions.empty()) {
            std::cerr << "A discovered function is missing cached ABI or IR data\n";
            return 1;
        }
    }

    const auto* addFunction = findFunction(arithmeticSession, "add");
    if(addFunction == nullptr) {
        std::cerr << "add was not discovered\n";
        return 1;
    }
    const auto* addAnalysis = arithmeticSession.abiAnalysisFor(addFunction->address);
    const auto* addIr = arithmeticSession.irFor(addFunction->address);
    if(addAnalysis == nullptr || addIr == nullptr || addAnalysis->parameters.size() < 2
       || !addAnalysis->returnsValue || addIr->parameters.size() < 2
       || !hasOpcode(*addIr, decompiler::IROpcode::Add)
       || !hasOpcode(*addIr, decompiler::IROpcode::Return)) {
        std::cerr << "add did not produce the expected ABI and arithmetic IR\n";
        return 1;
    }

    const auto* mainFunction = findFunction(arithmeticSession, "main");
    const auto* mainIr = mainFunction == nullptr ? nullptr : arithmeticSession.irFor(mainFunction->address);
    if(mainIr == nullptr || !hasOpcode(*mainIr, decompiler::IROpcode::Call)
       || !hasOpcode(*mainIr, decompiler::IROpcode::Return)) {
        std::cerr << "main did not produce call and return IR\n";
        return 1;
    }

    decompiler::AnalysisSession stackSession;
    if(!stackSession.analyze(argv[2])) {
        std::cerr << "Stack-local analysis failed: " << stackSession.errorMessage() << '\n';
        return 1;
    }
    const auto* localFunction = findFunction(stackSession, "use_local");
    const auto* localAnalysis =
        localFunction == nullptr ? nullptr : stackSession.abiAnalysisFor(localFunction->address);
    const auto* localIr =
        localFunction == nullptr ? nullptr : stackSession.irFor(localFunction->address);
    if(localAnalysis == nullptr || localIr == nullptr || localAnalysis->stackVariables.empty()
       || localIr->localVariables.empty() || !hasOpcode(*localIr, decompiler::IROpcode::Store)
       || !hasOpcode(*localIr, decompiler::IROpcode::Load)) {
        std::cerr << "use_local did not produce stack-local load/store IR\n";
        return 1;
    }

    return 0;
}
