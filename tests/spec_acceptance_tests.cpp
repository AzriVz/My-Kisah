#include "AnalysisSession.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if(condition) {
        return;
    }
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

const decompiler::FunctionInfo* findFunction(
    const decompiler::AnalysisSession& session,
    std::string_view namePart) {
    const auto function = std::find_if(
        session.functions().begin(), session.functions().end(), [&](const auto& candidate) {
            return candidate.name.find(namePart) != std::string::npos;
        });
    return function == session.functions().end() ? nullptr : &*function;
}

std::string analyzeFunction(const char* path, std::string_view namePart) {
    decompiler::AnalysisSession session;
    expect(session.analyze(path), std::string("analysis failed for ") + path);
    if(!session.isValid()) {
        return {};
    }

    const auto* function = findFunction(session, namePart);
    expect(function != nullptr, std::string("function was not discovered: ") + std::string(namePart));
    if(function == nullptr) {
        return {};
    }

    const auto* instructions = session.instructionsFor(function->address);
    const auto* graph = session.controlFlowGraphFor(function->address);
    const auto* pseudocode = session.pseudocodeFor(function->address);
    expect(instructions != nullptr && !instructions->empty(), "assembly cache is empty");
    expect(graph != nullptr && graph->entryBlock() != nullptr, "CFG entry is unavailable");
    expect(pseudocode != nullptr && !pseudocode->empty(), "pseudocode cache is empty");
    return pseudocode == nullptr ? std::string {} : *pseudocode;
}

bool containsAll(std::string_view text, std::initializer_list<std::string_view> fragments) {
    return std::all_of(fragments.begin(), fragments.end(), [text](std::string_view fragment) {
        return text.find(fragment) != std::string_view::npos;
    });
}

void expectContainsAll(
    std::string_view text,
    std::initializer_list<std::string_view> fragments,
    std::string_view message) {
    if(containsAll(text, fragments)) {
        return;
    }
    std::cerr << "Recovered output:\n" << text << '\n';
    expect(false, message);
}

} // namespace

int main(int argc, char* argv[]) {
    if(argc != 9) {
        std::cerr << "expected the seven mandatory samples and the string sample\n";
        return 2;
    }

    const auto arithmetic = analyzeFunction(argv[1], "add");
    expectContainsAll(
        arithmetic, {"arg0", "arg1", " + ", "return"},
        "arithmetic recovery is incomplete");

    const auto simpleIf = analyzeFunction(argv[2], "check");
    expectContainsAll(
        simpleIf, {"if (", "arg0", "return"},
        "simple-if recovery is incomplete");

    const auto ifElse = analyzeFunction(argv[3], "absolute_value");
    expectContainsAll(
        ifElse, {"if (", "else", "return"},
        "if/else recovery is incomplete");

    const auto loop = analyzeFunction(argv[4], "sum_to_n");
    expect(
        loop.find("while (") != std::string::npos
            || loop.find("do {") != std::string::npos
            || loop.find("goto block_") != std::string::npos,
        "loop recovery has neither a structured form nor an honest goto fallback");

    const auto functionCall = analyzeFunction(argv[5], "calculate");
    expectContainsAll(
        functionCall, {"multiply", "return"},
        "direct function-call recovery is incomplete");

    const auto nestedCondition = analyzeFunction(argv[6], "classify");
    expectContainsAll(
        nestedCondition, {"if (", "return"},
        "nested-condition recovery is incomplete");

    const auto recursion = analyzeFunction(argv[7], "factorial");
    expectContainsAll(
        recursion, {"if (", "factorial", "return"},
        "recursive-call recovery is incomplete");

    const auto stringLiteral = analyzeFunction(argv[8], "greeting");
    expectContainsAll(
        stringLiteral, {"return", "\"hello\""},
        "read-only string literal recovery is incomplete");

    return failures == 0 ? 0 : 1;
}
