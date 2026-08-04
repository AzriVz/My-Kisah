#include "ControlFlowGraph.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <utility>
#include <vector>

namespace decompiler {

static void appendUnique(std::vector<std::uint64_t>& addresses, std::uint64_t address) {
    if(std::find(addresses.begin(), addresses.end(), address) == addresses.end()) {
        addresses.push_back(address);
    }
}

bool ControlFlowGraph::build(std::vector<BasicBlock> blocks) {
    reset();
    if(blocks.empty()) {
        errorMessage_ = "A control-flow graph requires at least one basic block.";
        return false;
    }

    std::sort(blocks.begin(), blocks.end(), [](const BasicBlock& left, const BasicBlock& right) {
        return left.startAddress < right.startAddress;
    });

    blocks_ = std::move(blocks);
    blockIndex_.reserve(blocks_.size());
    for(std::size_t index = 0; index < blocks_.size(); ++index) {
        auto& block = blocks_[index];
        if(block.instructions.empty() || block.startAddress != block.instructions.front().address
           || block.endAddress < block.startAddress) {
            reset();
            errorMessage_ = "A basic block has an invalid instruction range.";
            return false;
        }

        const auto [position, inserted] = blockIndex_.emplace(block.startAddress, index);
        static_cast<void>(position);
        if(!inserted) {
            reset();
            errorMessage_ = "Basic block start addresses must be unique.";
            return false;
        }
    }

    buildEdges();
    analyzeReachability();
    buildTraversalOrders();
    analyzeDominatorsAndLoops();
    valid_ = true;
    return true;
}

void ControlFlowGraph::reset() noexcept {
    blocks_.clear();
    blockIndex_.clear();
    depthFirstOrder_.clear();
    reversePostOrder_.clear();
    exitBlocks_.clear();
    unreachableBlocks_.clear();
    backEdges_.clear();
    loopHeaders_.clear();
    dominators_.clear();
    errorMessage_.clear();
    valid_ = false;
}

bool ControlFlowGraph::isValid() const noexcept {
    return valid_;
}

std::string_view ControlFlowGraph::errorMessage() const noexcept {
    return errorMessage_;
}

const std::vector<BasicBlock>& ControlFlowGraph::blocks() const noexcept {
    return blocks_;
}

const BasicBlock* ControlFlowGraph::entryBlock() const noexcept {
    return blocks_.empty() ? nullptr : &blocks_.front();
}

const BasicBlock* ControlFlowGraph::blockAt(std::uint64_t startAddress) const noexcept {
    const auto block = blockIndex_.find(startAddress);
    if(block == blockIndex_.end()) {
        return nullptr;
    }
    return &blocks_[block->second];
}

const BasicBlock* ControlFlowGraph::blockContaining(std::uint64_t address) const noexcept {
    const auto candidate = std::upper_bound(
        blocks_.begin(),
        blocks_.end(),
        address,
        [](std::uint64_t requestedAddress, const BasicBlock& block) {
            return requestedAddress < block.startAddress;
        });
    if(candidate == blocks_.begin()) {
        return nullptr;
    }

    const auto& block = *std::prev(candidate);
    const bool atSaturatedEnd = address == block.endAddress && !block.instructions.empty()
                                && block.instructions.back().address == block.endAddress;
    if(address < block.startAddress || (address >= block.endAddress && !atSaturatedEnd)) {
        return nullptr;
    }
    return &block;
}

const std::vector<std::uint64_t>& ControlFlowGraph::depthFirstOrder() const noexcept {
    return depthFirstOrder_;
}

const std::vector<std::uint64_t>& ControlFlowGraph::reversePostOrder() const noexcept {
    return reversePostOrder_;
}

const std::vector<std::uint64_t>& ControlFlowGraph::exitBlocks() const noexcept {
    return exitBlocks_;
}

const std::vector<std::uint64_t>& ControlFlowGraph::unreachableBlocks() const noexcept {
    return unreachableBlocks_;
}

const std::vector<std::pair<std::uint64_t, std::uint64_t>>&
ControlFlowGraph::backEdges() const noexcept {
    return backEdges_;
}

const std::vector<std::uint64_t>& ControlFlowGraph::loopHeaders() const noexcept {
    return loopHeaders_;
}

bool ControlFlowGraph::dominates(
    std::uint64_t dominatorAddress,
    std::uint64_t blockAddress) const noexcept {
    const auto blockDominators = dominators_.find(blockAddress);
    return blockDominators != dominators_.end()
           && blockDominators->second.contains(dominatorAddress);
}

void ControlFlowGraph::buildEdges() {
    exitBlocks_.clear();
    for(auto& block : blocks_) {
        block.successors.clear();
        block.predecessors.clear();
        block.isReachable = false;
        block.isExit = false;
        block.hasUnresolvedSuccessor = false;
    }

    for(std::size_t index = 0; index < blocks_.size(); ++index) {
        auto& block = blocks_[index];
        const auto& terminator = block.instructions.back();
        const auto addDirectTarget = [&]() {
            if(!terminator.directTarget || !blockIndex_.contains(*terminator.directTarget)) {
                block.hasUnresolvedSuccessor = true;
                return;
            }
            appendUnique(block.successors, *terminator.directTarget);
        };
        const auto addFallthrough = [&]() {
            if(index + 1 < blocks_.size()) {
                appendUnique(block.successors, blocks_[index + 1].startAddress);
            }
        };

        switch(terminator.kind) {
        case InstructionKind::ConditionalJump:
            addDirectTarget();
            addFallthrough();
            break;
        case InstructionKind::UnconditionalJump:
            addDirectTarget();
            break;
        case InstructionKind::IndirectJump:
            block.hasUnresolvedSuccessor = true;
            break;
        case InstructionKind::Return:
            break;
        case InstructionKind::Normal:
        case InstructionKind::Call:
        case InstructionKind::Invalid:
            addFallthrough();
            break;
        }
    }

    for(auto& block : blocks_) {
        for(const auto successorAddress : block.successors) {
            auto& successor = blocks_[blockIndex_.at(successorAddress)];
            appendUnique(successor.predecessors, block.startAddress);
        }
    }

    for(auto& block : blocks_) {
        block.isExit = block.successors.empty() && !block.hasUnresolvedSuccessor;
        if(block.isExit) {
            exitBlocks_.push_back(block.startAddress);
        }
    }
}

void ControlFlowGraph::analyzeReachability() {
    unreachableBlocks_.clear();
    if(blocks_.empty()) {
        return;
    }

    std::vector<std::uint64_t> pending {blocks_.front().startAddress};
    std::unordered_set<std::uint64_t> visited;
    visited.reserve(blocks_.size());

    while(!pending.empty()) {
        const auto address = pending.back();
        pending.pop_back();
        if(!visited.insert(address).second) {
            continue;
        }

        auto& block = blocks_[blockIndex_.at(address)];
        block.isReachable = true;
        for(auto successor = block.successors.rbegin(); successor != block.successors.rend();
            ++successor) {
            pending.push_back(*successor);
        }
    }

    for(const auto& block : blocks_) {
        if(!block.isReachable) {
            unreachableBlocks_.push_back(block.startAddress);
        }
    }
}

void ControlFlowGraph::buildTraversalOrders() {
    depthFirstOrder_.clear();
    reversePostOrder_.clear();
    if(blocks_.empty()) {
        return;
    }

    std::unordered_set<std::uint64_t> visited;
    visited.reserve(blocks_.size());
    std::vector<std::uint64_t> pending {blocks_.front().startAddress};
    while(!pending.empty()) {
        const auto address = pending.back();
        pending.pop_back();
        if(!visited.insert(address).second) {
            continue;
        }

        depthFirstOrder_.push_back(address);
        const auto& successors = blocks_[blockIndex_.at(address)].successors;
        for(auto successor = successors.rbegin(); successor != successors.rend(); ++successor) {
            pending.push_back(*successor);
        }
    }

    struct TraversalFrame {
        std::uint64_t address = 0;
        std::size_t nextSuccessor = 0;
    };

    visited.clear();
    std::vector<TraversalFrame> frames {{blocks_.front().startAddress, 0}};
    visited.insert(blocks_.front().startAddress);
    std::vector<std::uint64_t> postOrder;
    postOrder.reserve(depthFirstOrder_.size());

    while(!frames.empty()) {
        auto& frame = frames.back();
        const auto& successors = blocks_[blockIndex_.at(frame.address)].successors;
        if(frame.nextSuccessor < successors.size()) {
            const auto successor = successors[frame.nextSuccessor++];
            if(visited.insert(successor).second) {
                frames.push_back({successor, 0});
            }
            continue;
        }

        postOrder.push_back(frame.address);
        frames.pop_back();
    }

    reversePostOrder_.assign(postOrder.rbegin(), postOrder.rend());
}

void ControlFlowGraph::analyzeDominatorsAndLoops() {
    dominators_.clear();
    backEdges_.clear();
    loopHeaders_.clear();
    if(blocks_.empty()) {
        return;
    }

    std::unordered_set<std::uint64_t> reachable;
    reachable.reserve(depthFirstOrder_.size());
    for(const auto address : depthFirstOrder_) {
        reachable.insert(address);
    }

    const auto entryAddress = blocks_.front().startAddress;
    for(const auto address : depthFirstOrder_) {
        if(address == entryAddress) {
            dominators_[address] = {address};
        } else {
            dominators_[address] = reachable;
        }
    }

    bool changed = true;
    while(changed) {
        changed = false;
        for(const auto address : reversePostOrder_) {
            if(address == entryAddress) {
                continue;
            }

            const auto& block = blocks_[blockIndex_.at(address)];
            std::unordered_set<std::uint64_t> intersection;
            bool hasReachablePredecessor = false;
            for(const auto predecessor : block.predecessors) {
                if(!reachable.contains(predecessor)) {
                    continue;
                }

                if(!hasReachablePredecessor) {
                    intersection = dominators_.at(predecessor);
                    hasReachablePredecessor = true;
                    continue;
                }

                for(auto candidate = intersection.begin(); candidate != intersection.end();) {
                    if(!dominators_.at(predecessor).contains(*candidate)) {
                        candidate = intersection.erase(candidate);
                    } else {
                        ++candidate;
                    }
                }
            }

            if(!hasReachablePredecessor) {
                continue;
            }
            intersection.insert(address);
            if(intersection != dominators_.at(address)) {
                dominators_[address] = std::move(intersection);
                changed = true;
            }
        }
    }

    std::unordered_set<std::uint64_t> headers;
    for(const auto& block : blocks_) {
        if(!block.isReachable) {
            continue;
        }
        for(const auto successor : block.successors) {
            if(dominates(successor, block.startAddress)) {
                backEdges_.emplace_back(block.startAddress, successor);
                headers.insert(successor);
            }
        }
    }

    loopHeaders_.assign(headers.begin(), headers.end());
    std::sort(loopHeaders_.begin(), loopHeaders_.end());
}

} // namespace decompiler
