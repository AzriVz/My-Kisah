#include "AnalysisSession.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <string_view>

static const decompiler::FunctionInfo* findFunction(
    const decompiler::AnalysisSession& session,
    std::string_view namePart) {
    const auto function = std::find_if(
        session.functions().begin(), session.functions().end(), [&](const auto& candidate) {
            return candidate.name.find(namePart) != std::string::npos;
        });
    return function == session.functions().end() ? nullptr : &*function;
}

static const std::string* analyzeFunction(
    const char* path,
    std::string_view namePart,
    decompiler::AnalysisSession& session) {
    if(!session.analyze(path)) {
        std::cerr << "Analysis failed for " << path << ": " << session.errorMessage() << '\n';
        return nullptr;
    }
    for(const auto& function : session.functions()) {
        if(session.dataFlowFor(function.address) == nullptr
           || session.pseudocodeFor(function.address) == nullptr) {
            std::cerr << "A discovered function is missing phase 6 cache entries\n";
            return nullptr;
        }
    }
    const auto* function = findFunction(session, namePart);
    if(function == nullptr) {
        std::cerr << "Function containing " << namePart << " was not discovered\n";
        return nullptr;
    }
    return session.pseudocodeFor(function->address);
}

int main(int argc, char* argv[]) {
    if(argc < 5) {
        std::cerr << "Arithmetic, loop, stack-local, and branching samples are required\n";
        return 1;
    }

    decompiler::AnalysisSession arithmeticSession;
    const auto* add = analyzeFunction(argv[1], "add", arithmeticSession);
    if(add == nullptr || add->find("arg0") == std::string::npos
       || add->find("arg1") == std::string::npos
       || add->find(" + ") == std::string::npos
       || add->find("return") == std::string::npos) {
        std::cerr << "Arithmetic sample pseudocode is incomplete\n";
        return 1;
    }
    const auto* mainFunction = findFunction(arithmeticSession, "main");
    const auto* mainPseudocode = mainFunction == nullptr
                                     ? nullptr
                                     : arithmeticSession.pseudocodeFor(mainFunction->address);
    if(mainPseudocode == nullptr || mainPseudocode->find("(2, 3)") == std::string::npos
       || mainPseudocode->find("return result") == std::string::npos) {
        std::cerr << "Compiler-produced call pseudocode is incomplete\n";
        return 1;
    }

    decompiler::AnalysisSession loopSession;
    const auto* loop = analyzeFunction(argv[2], "sum_to_n", loopSession);
    if(loop == nullptr || loop->find("do {") == std::string::npos
       || loop->find("goto block_") == std::string::npos) {
        std::cerr << "Loop sample should use structured loop or honest goto fallback\n";
        return 1;
    }

    decompiler::AnalysisSession stackSession;
    const auto* stackLocal = analyzeFunction(argv[3], "use_local", stackSession);
    if(stackLocal == nullptr || stackLocal->find("local_4") == std::string::npos
       || stackLocal->find("return local_4;") == std::string::npos) {
        std::cerr << "Stack-local pseudocode is incomplete\n";
        return 1;
    }

    decompiler::AnalysisSession branchSession;
    const auto* branch = analyzeFunction(argv[4], "choose", branchSession);
    if(branch == nullptr || branch->find("if (") == std::string::npos
       || branch->find("else") == std::string::npos
       || branch->find("incrementi(arg0)") == std::string::npos
       || branch->find("decrementi(arg0)") == std::string::npos) {
        std::cerr << "Branching sample did not reconstruct if/else\n";
        return 1;
    }

    return 0;
}
