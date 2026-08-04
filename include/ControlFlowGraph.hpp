#pragma once

#include "BasicBlock.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace decompiler {

class ControlFlowGraph final {
public:
    bool build(std::vector<BasicBlock> blocks);
    void reset() noexcept;

    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] std::string_view errorMessage() const noexcept;
    [[nodiscard]] const std::vector<BasicBlock>& blocks() const noexcept;
    [[nodiscard]] const BasicBlock* entryBlock() const noexcept;
    [[nodiscard]] const BasicBlock* blockAt(std::uint64_t startAddress) const noexcept;
    [[nodiscard]] const BasicBlock* blockContaining(std::uint64_t address) const noexcept;

    [[nodiscard]] const std::vector<std::uint64_t>& depthFirstOrder() const noexcept;
    [[nodiscard]] const std::vector<std::uint64_t>& reversePostOrder() const noexcept;
    [[nodiscard]] const std::vector<std::uint64_t>& exitBlocks() const noexcept;
    [[nodiscard]] const std::vector<std::uint64_t>& unreachableBlocks() const noexcept;
    [[nodiscard]] const std::vector<std::pair<std::uint64_t, std::uint64_t>>&
    backEdges() const noexcept;
    [[nodiscard]] const std::vector<std::uint64_t>& loopHeaders() const noexcept;
    [[nodiscard]] bool dominates(
        std::uint64_t dominatorAddress,
        std::uint64_t blockAddress) const noexcept;

private:
    void buildEdges();
    void analyzeReachability();
    void buildTraversalOrders();
    void analyzeDominatorsAndLoops();

    std::vector<BasicBlock> blocks_;
    std::unordered_map<std::uint64_t, std::size_t> blockIndex_;
    std::vector<std::uint64_t> depthFirstOrder_;
    std::vector<std::uint64_t> reversePostOrder_;
    std::vector<std::uint64_t> exitBlocks_;
    std::vector<std::uint64_t> unreachableBlocks_;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> backEdges_;
    std::vector<std::uint64_t> loopHeaders_;
    std::unordered_map<std::uint64_t, std::unordered_set<std::uint64_t>> dominators_;
    std::string errorMessage_;
    bool valid_ = false;
};

} // namespace decompiler

