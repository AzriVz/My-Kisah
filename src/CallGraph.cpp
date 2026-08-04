#include "CallGraph.hpp"

#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>
#include <utility>

namespace decompiler {

static std::string externalFunctionName(std::uint64_t address) {
    std::ostringstream name;
    name << "external_0x" << std::hex << std::nouppercase << address;
    return name.str();
}

bool CallGraph::build(
    const std::vector<FunctionInfo>& functions,
    const std::unordered_map<std::uint64_t, std::vector<Instruction>>& instructions) {
    clear();
    nodes_.reserve(functions.size());
    nodeIndices_.reserve(functions.size());

    for(const auto& function : functions) {
        if(nodeIndices_.contains(function.address)) {
            clear();
            errorMessage_ = "Call graph received duplicate function addresses.";
            return false;
        }
        nodeIndices_.emplace(function.address, nodes_.size());
        nodes_.push_back(CallGraphNode {
            .address = function.address,
            .name = function.name,
            .size = function.size,
            .isExternal = false,
        });
    }

    std::set<std::pair<std::uint64_t, std::uint64_t>> relationships;
    for(const auto& function : functions) {
        const auto cachedInstructions = instructions.find(function.address);
        if(cachedInstructions == instructions.end()) {
            clear();
            errorMessage_ = "Call graph is missing instructions for a discovered function.";
            return false;
        }

        for(const auto& instruction : cachedInstructions->second) {
            if(instruction.kind != InstructionKind::Call || !instruction.directTarget) {
                continue;
            }

            const auto target = *instruction.directTarget;
            if(!nodeIndices_.contains(target)) {
                nodeIndices_.emplace(target, nodes_.size());
                nodes_.push_back(CallGraphNode {
                    .address = target,
                    .name = externalFunctionName(target),
                    .size = 0,
                    .isExternal = true,
                });
            }

            if(!relationships.emplace(function.address, target).second) {
                continue;
            }
            edges_.push_back(CallGraphEdge {
                .callerAddress = function.address,
                .calleeAddress = target,
                .callSiteAddress = instruction.address,
            });
        }
    }

    std::sort(edges_.begin(), edges_.end(), [](const auto& left, const auto& right) {
        if(left.callerAddress != right.callerAddress) {
            return left.callerAddress < right.callerAddress;
        }
        return left.calleeAddress < right.calleeAddress;
    });
    return true;
}

void CallGraph::clear() noexcept {
    nodes_.clear();
    edges_.clear();
    nodeIndices_.clear();
    errorMessage_.clear();
}

const std::vector<CallGraphNode>& CallGraph::nodes() const noexcept {
    return nodes_;
}

const std::vector<CallGraphEdge>& CallGraph::edges() const noexcept {
    return edges_;
}

const CallGraphNode* CallGraph::nodeAt(std::uint64_t address) const noexcept {
    const auto node = nodeIndices_.find(address);
    if(node == nodeIndices_.end()) {
        return nullptr;
    }
    return &nodes_[node->second];
}

std::vector<const CallGraphEdge*>
CallGraph::outgoingEdges(std::uint64_t address) const {
    std::vector<const CallGraphEdge*> result;
    for(const auto& edge : edges_) {
        if(edge.callerAddress == address) {
            result.push_back(&edge);
        }
    }
    return result;
}

std::string_view CallGraph::errorMessage() const noexcept {
    return errorMessage_;
}

} // namespace decompiler
