#pragma once

#include <QPointF>
#include <QRectF>
#include <QSizeF>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace decompiler {

class CallGraph;

struct CallGraphComponent {
    std::size_t id = 0;
    std::vector<std::uint64_t> nodeAddresses;
    std::uint64_t representativeAddress = 0;
    std::uint64_t secondaryAddress = 0;
    QRectF sceneBounds;
};

struct CallGraphLayout {
    std::unordered_map<std::uint64_t, QPointF> nodePositions;
    std::vector<CallGraphComponent> components;
};

class CallGraphLayoutEngine final {
public:
    [[nodiscard]] CallGraphLayout
    layout(const CallGraph& graph, const QSizeF& nodeSize) const;
};

} // namespace decompiler
