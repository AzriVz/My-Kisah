#pragma once

#include <cstdint>
#include <string>

namespace decompiler {

enum class FunctionSource {
    SymbolTable,
    DynamicSymbolTable,
    EntryPoint,
    DirectCallTarget,
    Heuristic,
};

struct FunctionInfo {
    std::string name;
    std::uint64_t address = 0;
    std::uint64_t size = 0;
    FunctionSource source = FunctionSource::Heuristic;
    bool sizeIsEstimated = true;
};

} // namespace decompiler

