#pragma once

#include "FunctionInfo.hpp"
#include "Instruction.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace decompiler {

struct CallGraphNode {
    std::uint64_t address = 0;
    std::string name;
    std::uint64_t size = 0;
    bool isExternal = false;
};

struct CallGraphEdge {
    std::uint64_t callerAddress = 0;
    std::uint64_t calleeAddress = 0;
    std::uint64_t callSiteAddress = 0;
};

class CallGraph final {
public:
    bool build(
        const std::vector<FunctionInfo>& functions,
        const std::unordered_map<std::uint64_t, std::vector<Instruction>>& instructions);
    void clear() noexcept;

    [[nodiscard]] const std::vector<CallGraphNode>& nodes() const noexcept;
    [[nodiscard]] const std::vector<CallGraphEdge>& edges() const noexcept;
    [[nodiscard]] const CallGraphNode* nodeAt(std::uint64_t address) const noexcept;
    [[nodiscard]] std::vector<const CallGraphEdge*>
    outgoingEdges(std::uint64_t address) const;
    [[nodiscard]] std::string_view errorMessage() const noexcept;

private:
    std::vector<CallGraphNode> nodes_;
    std::vector<CallGraphEdge> edges_;
    std::unordered_map<std::uint64_t, std::size_t> nodeIndices_;
    std::string errorMessage_;
};

} // namespace decompiler
