#pragma once

#include "BasicBlock.hpp"

#include <span>
#include <vector>

namespace decompiler {

class BasicBlockBuilder final {
public:
    [[nodiscard]] std::vector<BasicBlock>
    build(std::span<const Instruction> instructions) const;
};

} // namespace decompiler

