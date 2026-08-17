#include "AnalysisSession.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <string_view>

static int failures = 0;

static void expect(bool condition, std::string_view message) {
    if(!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

static void testBinary(const char* path, std::string_view optimization) {
    decompiler::AnalysisSession session;
    expect(session.analyze(path), std::string(optimization).append(" PIE should analyze"));
    if(!session.isValid()) {
        std::cerr << "analysis error: " << session.errorMessage() << '\n';
        return;
    }

    expect(
        session.elfLoader().metadata().isPositionIndependent,
        std::string(optimization).append(" binary should be identified as PIE"));
    expect(!session.functions().empty(), "optimized binary should still expose functions");
    expect(!session.callGraph().nodes().empty(), "optimized binary should produce a call graph");

    bool foundRipRelative = false;
    bool resolvedRipRelative = false;
    bool foundPseudocode = false;
    for(const auto& function : session.functions()) {
        if(const auto* pseudocode = session.pseudocodeFor(function.address)) {
            foundPseudocode = foundPseudocode || !pseudocode->empty();
        }
        const auto* instructions = session.instructionsFor(function.address);
        const auto* ir = session.irFor(function.address);
        if(instructions == nullptr) {
            continue;
        }
        for(const auto& instruction : *instructions) {
            for(const auto& operand : instruction.operands) {
                if(operand.kind == decompiler::OperandKind::Memory
                   && operand.memory.baseRegister == "rip") {
                    foundRipRelative = true;
                    if(ir != nullptr) {
                        for(const auto& lifted : ir->instructions) {
                            if(lifted.sourceAddress != instruction.address) {
                                continue;
                            }
                            resolvedRipRelative = resolvedRipRelative
                                                  || std::any_of(
                                                      lifted.operands.begin(),
                                                      lifted.operands.end(),
                                                      [](const auto& value) {
                                                          return value.address.has_value();
                                                      });
                            resolvedRipRelative = resolvedRipRelative
                                                  || (lifted.destination
                                                      && lifted.destination->address);
                        }
                    }
                }
            }
        }
    }
    expect(foundRipRelative, "optimized PIE should retain RIP-relative memory operands");
    expect(resolvedRipRelative, "RIP-relative operands should resolve to virtual addresses in IR");
    expect(foundPseudocode, "optimized input should produce non-empty pseudocode");
}

int main(int argc, char* argv[]) {
    if(argc != 3) {
        std::cerr << "expected O2 and O3 sample paths\n";
        return 2;
    }
    testBinary(argv[1], "O2");
    testBinary(argv[2], "O3");
    return failures == 0 ? 0 : 1;
}
