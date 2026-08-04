#pragma once

#include "Instruction.hpp"

#include <cstdint>
#include <vector>

namespace decompiler {

struct BasicBlock {
    std::uint64_t startAddress = 0;
    // Exclusive end: one byte past the final instruction when representable.
    std::uint64_t endAddress = 0;
    std::vector<Instruction> instructions;
    std::vector<std::uint64_t> successors;
    std::vector<std::uint64_t> predecessors;
    bool isReachable = false;
    bool isExit = false;
    bool hasUnresolvedSuccessor = false;
};

} // namespace decompiler

