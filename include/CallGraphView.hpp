#pragma once

#include <QGraphicsView>

#include <cstdint>
#include <functional>
#include <unordered_map>

class QGraphicsItem;
class QGraphicsRectItem;
class QMouseEvent;
class QWheelEvent;

namespace decompiler {

class CallGraph;

class CallGraphView final : public QGraphicsView {
public:
    explicit CallGraphView(QWidget* parent = nullptr);

    void setGraph(const CallGraph& graph);
    void clearGraph();
    void setActiveFunction(std::uint64_t address);
    void setNodeActivationHandler(std::function<void(std::uint64_t)> handler);
    void zoomIn();
    void zoomOut();
    void resetZoom();

    [[nodiscard]] std::size_t nodeCount() const noexcept;
    [[nodiscard]] std::size_t edgeCount() const noexcept;
    [[nodiscard]] qreal zoomFactor() const noexcept;
    [[nodiscard]] std::uint64_t activeFunction() const noexcept;
    [[nodiscard]] bool activateNode(std::uint64_t address);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void applyNodeStyle(QGraphicsRectItem* item, bool active, bool external);
    [[nodiscard]] QGraphicsRectItem* nodeItemFor(QGraphicsItem* item) const noexcept;

    std::unordered_map<std::uint64_t, QGraphicsRectItem*> nodeItems_;
    std::function<void(std::uint64_t)> nodeActivationHandler_;
    std::uint64_t activeFunction_ = 0;
    std::size_t edgeCount_ = 0;
    QPoint pressPosition_;
};

} // namespace decompiler
