#include "CallGraphLayoutEngine.hpp"

#include "CallGraph.hpp"

#include <algorithm>
#include <deque>
#include <unordered_set>
#include <utility>

namespace decompiler {

static std::unordered_map<std::uint64_t, std::vector<std::uint64_t>>
undirectedAdjacency(const CallGraph& graph) {
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> adjacency;
    adjacency.reserve(graph.nodes().size());
    for(const auto& node : graph.nodes()) {
        adjacency.emplace(node.address, std::vector<std::uint64_t> {});
    }
    for(const auto& edge : graph.edges()) {
        if(edge.callerAddress == edge.calleeAddress) {
            continue;
        }
        adjacency[edge.callerAddress].push_back(edge.calleeAddress);
        adjacency[edge.calleeAddress].push_back(edge.callerAddress);
    }
    for(auto& [address, neighbours] : adjacency) {
        static_cast<void>(address);
        std::sort(neighbours.begin(), neighbours.end());
        neighbours.erase(std::unique(neighbours.begin(), neighbours.end()), neighbours.end());
    }
    return adjacency;
}

static std::vector<std::vector<std::uint64_t>> connectedComponents(
    const CallGraph& graph,
    const std::unordered_map<std::uint64_t, std::vector<std::uint64_t>>& adjacency) {
    std::vector<std::uint64_t> addresses;
    addresses.reserve(graph.nodes().size());
    for(const auto& node : graph.nodes()) {
        addresses.push_back(node.address);
    }
    std::sort(addresses.begin(), addresses.end());

    std::vector<std::vector<std::uint64_t>> result;
    std::unordered_set<std::uint64_t> visited;
    visited.reserve(addresses.size());
    for(const auto address : addresses) {
        if(!visited.insert(address).second) {
            continue;
        }

        std::vector<std::uint64_t> component;
        std::deque<std::uint64_t> pending {address};
        while(!pending.empty()) {
            const auto current = pending.front();
            pending.pop_front();
            component.push_back(current);
            const auto neighbours = adjacency.find(current);
            if(neighbours == adjacency.end()) {
                continue;
            }
            for(const auto neighbour : neighbours->second) {
                if(visited.insert(neighbour).second) {
                    pending.push_back(neighbour);
                }
            }
        }
        std::sort(component.begin(), component.end());
        result.push_back(std::move(component));
    }
    return result;
}

static std::unordered_map<std::uint64_t, std::size_t>
incomingCounts(const CallGraph& graph) {
    std::unordered_map<std::uint64_t, std::size_t> counts;
    counts.reserve(graph.nodes().size());
    for(const auto& node : graph.nodes()) {
        counts.emplace(node.address, 0);
    }
    for(const auto& edge : graph.edges()) {
        ++counts[edge.calleeAddress];
    }
    return counts;
}

static std::uint64_t chooseRoot(
    const CallGraph& graph,
    const std::vector<std::uint64_t>& component,
    const std::unordered_map<std::uint64_t, std::size_t>& incoming) {
    for(const auto address : component) {
        const auto* node = graph.nodeAt(address);
        if(node != nullptr && !node->isExternal && node->name == "main") {
            return address;
        }
    }
    for(const auto address : component) {
        const auto* node = graph.nodeAt(address);
        if(node != nullptr && !node->isExternal && incoming.at(address) == 0) {
            return address;
        }
    }

    return *std::min_element(
        component.begin(), component.end(), [&](const auto left, const auto right) {
            const auto leftIncoming = incoming.at(left);
            const auto rightIncoming = incoming.at(right);
            if(leftIncoming != rightIncoming) {
                return leftIncoming < rightIncoming;
            }
            const auto* leftNode = graph.nodeAt(left);
            const auto* rightNode = graph.nodeAt(right);
            if(leftNode != nullptr && rightNode != nullptr
               && leftNode->isExternal != rightNode->isExternal) {
                return !leftNode->isExternal;
            }
            return left < right;
        });
}

static std::unordered_map<std::uint64_t, std::size_t> componentLayers(
    const CallGraph& graph,
    const std::vector<std::uint64_t>& component,
    std::uint64_t root,
    const std::unordered_map<std::uint64_t, std::vector<std::uint64_t>>& adjacency) {
    std::unordered_set<std::uint64_t> componentSet(component.begin(), component.end());
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> outgoing;
    std::unordered_map<std::uint64_t, std::size_t> incoming;
    outgoing.reserve(component.size());
    incoming.reserve(component.size());
    for(const auto address : component) {
        outgoing.emplace(address, std::vector<std::uint64_t> {});
        incoming.emplace(address, 0);
    }
    for(const auto& edge : graph.edges()) {
        if(componentSet.contains(edge.callerAddress)
           && componentSet.contains(edge.calleeAddress)
           && edge.callerAddress != edge.calleeAddress) {
            outgoing[edge.callerAddress].push_back(edge.calleeAddress);
            ++incoming[edge.calleeAddress];
        }
    }

    std::unordered_map<std::uint64_t, std::size_t> layers;
    layers.reserve(component.size());
    std::deque<std::uint64_t> pending;
    for(const auto address : component) {
        if(incoming[address] == 0) {
            layers.emplace(address, 0);
            pending.push_back(address);
        }
    }
    if(layers.emplace(root, 0).second) {
        pending.push_front(root);
    }
    while(!pending.empty()) {
        const auto current = pending.front();
        pending.pop_front();
        for(const auto target : outgoing[current]) {
            if(layers.emplace(target, layers[current] + 1).second) {
                pending.push_back(target);
            }
        }
    }

    std::unordered_map<std::uint64_t, std::size_t> undirectedDistance;
    undirectedDistance.emplace(root, 0);
    pending.push_back(root);
    while(!pending.empty()) {
        const auto current = pending.front();
        pending.pop_front();
        const auto neighbours = adjacency.find(current);
        if(neighbours == adjacency.end()) {
            continue;
        }
        for(const auto neighbour : neighbours->second) {
            if(!componentSet.contains(neighbour)) {
                continue;
            }
            if(undirectedDistance.emplace(neighbour, undirectedDistance[current] + 1).second) {
                pending.push_back(neighbour);
            }
        }
    }
    for(const auto address : component) {
        if(!layers.contains(address)) {
            layers.emplace(address, undirectedDistance[address]);
        }
    }
    return layers;
}

CallGraphLayout CallGraphLayoutEngine::layout(
    const CallGraph& graph,
    const QSizeF& nodeSize) const {
    std::unordered_map<std::uint64_t, QSizeF> nodeSizes;
    nodeSizes.reserve(graph.nodes().size());
    for(const auto& node : graph.nodes()) {
        nodeSizes.emplace(node.address, nodeSize);
    }
    return layout(graph, nodeSizes, nodeSize);
}

CallGraphLayout CallGraphLayoutEngine::layout(
    const CallGraph& graph,
    const std::unordered_map<std::uint64_t, QSizeF>& nodeSizes,
    const QSizeF& fallbackNodeSize) const {
    CallGraphLayout result;
    if(graph.nodes().empty()) {
        return result;
    }

    constexpr qreal horizontalSpacing = 66.0;
    constexpr qreal verticalSpacing = 86.0;
    constexpr qreal componentSpacing = 150.0;
    constexpr qreal maximumRowWidth = 2200.0;
    const auto adjacency = undirectedAdjacency(graph);
    auto components = connectedComponents(graph, adjacency);
    const auto incoming = incomingCounts(graph);
    const auto sizeFor = [&nodeSizes, &fallbackNodeSize](std::uint64_t address) {
        const auto size = nodeSizes.find(address);
        return size == nodeSizes.end() ? fallbackNodeSize : size->second;
    };

    std::sort(components.begin(), components.end(), [&](const auto& left, const auto& right) {
        const auto leftRoot = chooseRoot(graph, left, incoming);
        const auto rightRoot = chooseRoot(graph, right, incoming);
        const auto* leftNode = graph.nodeAt(leftRoot);
        const auto* rightNode = graph.nodeAt(rightRoot);
        const bool leftMain = leftNode != nullptr && leftNode->name == "main";
        const bool rightMain = rightNode != nullptr && rightNode->name == "main";
        return leftMain != rightMain ? leftMain : leftRoot < rightRoot;
    });

    qreal componentX = 0.0;
    qreal componentY = 0.0;
    qreal rowHeight = 0.0;
    result.nodePositions.reserve(graph.nodes().size());
    result.components.reserve(components.size());

    for(std::size_t componentIndex = 0; componentIndex < components.size(); ++componentIndex) {
        const auto& addresses = components[componentIndex];
        const auto root = chooseRoot(graph, addresses, incoming);
        const auto layers = componentLayers(graph, addresses, root, adjacency);
        std::size_t maximumDepth = 0;
        for(const auto& [address, depth] : layers) {
            static_cast<void>(address);
            maximumDepth = std::max(maximumDepth, depth);
        }

        std::vector<std::vector<std::uint64_t>> grouped(maximumDepth + 1);
        for(const auto address : addresses) {
            grouped[layers.at(address)].push_back(address);
        }
        std::vector<qreal> layerWidths(grouped.size(), 0.0);
        std::vector<qreal> layerHeights(grouped.size(), 0.0);
        for(auto& layer : grouped) {
            std::sort(layer.begin(), layer.end());
        }
        qreal width = 0.0;
        qreal height = 0.0;
        for(std::size_t depth = 0; depth < grouped.size(); ++depth) {
            const auto& layer = grouped[depth];
            for(const auto address : layer) {
                const auto nodeSize = sizeFor(address);
                layerWidths[depth] += nodeSize.width();
                layerHeights[depth] = std::max(
                    layerHeights[depth], nodeSize.height());
            }
            if(layer.size() > 1) {
                layerWidths[depth] += static_cast<qreal>(layer.size() - 1)
                                      * horizontalSpacing;
            }
            width = std::max(width, layerWidths[depth]);
            height += layerHeights[depth];
        }
        if(grouped.size() > 1) {
            height += static_cast<qreal>(grouped.size() - 1) * verticalSpacing;
        }
        if(componentX > 0.0 && componentX + width > maximumRowWidth) {
            componentX = 0.0;
            componentY += rowHeight + componentSpacing;
            rowHeight = 0.0;
        }

        QRectF bounds;
        auto layerY = componentY;
        for(std::size_t depth = 0; depth < grouped.size(); ++depth) {
            const auto& layer = grouped[depth];
            if(layer.empty()) {
                continue;
            }
            auto nodeX = componentX + (width - layerWidths[depth]) / 2.0;
            for(std::size_t index = 0; index < layer.size(); ++index) {
                const auto nodeSize = sizeFor(layer[index]);
                const QPointF position(nodeX, layerY);
                result.nodePositions.emplace(layer[index], position);
                const QRectF nodeBounds(position, nodeSize);
                bounds = bounds.isNull() ? nodeBounds : bounds.united(nodeBounds);
                nodeX += nodeSize.width() + horizontalSpacing;
            }
            layerY += layerHeights[depth] + verticalSpacing;
        }

        result.components.push_back(CallGraphComponent {
            .id = componentIndex,
            .nodeAddresses = addresses,
            .representativeAddress = root,
            .secondaryAddress = addresses.size() > 1 ? addresses.back() : root,
            .sceneBounds = bounds.adjusted(-44.0, -44.0, 44.0, 44.0),
        });
        componentX += width + componentSpacing;
        rowHeight = std::max(rowHeight, height);
    }

    return result;
}

} // namespace decompiler
