#pragma once

#include "CallGraphLayoutEngine.hpp"
#include "Instruction.hpp"

#include <QGraphicsView>
#include <QPainterPath>

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

class QContextMenuEvent;
class QGraphicsItem;
class QMouseEvent;
class QResizeEvent;
class QShowEvent;
class QWheelEvent;

namespace decompiler {

class CallGraph;
class CallGraphEdgeItem;
class CallGraphNodeItem;

class CallGraphView final : public QGraphicsView {
public:
    explicit CallGraphView(QWidget* parent = nullptr);
    ~CallGraphView() override;

    void setGraph(const CallGraph& graph);
    void clearGraph();
    void refreshLayout();
    void setActiveFunction(std::uint64_t address);
    void setNodeActivationHandler(std::function<void(std::uint64_t)> handler);
    void setInstructionProvider(
        std::function<const std::vector<Instruction>*(std::uint64_t)> provider);
    void setSelectionChangedHandler(
        std::function<void(std::optional<std::uint64_t>)> handler);
    void setViewportChangedHandler(std::function<void()> handler);

    void zoomIn();
    void zoomOut();
    void resetZoom();
    void fitAll();
    void fitSelection();
    bool fitComponent(std::size_t componentIndex);
    bool centerOnNode(std::uint64_t address);
    void centerOnScenePoint(const QPointF& point);

    [[nodiscard]] std::size_t nodeCount() const noexcept;
    [[nodiscard]] std::size_t edgeCount() const noexcept;
    [[nodiscard]] qreal zoomFactor() const noexcept;
    [[nodiscard]] std::uint64_t activeFunction() const noexcept;
    [[nodiscard]] std::optional<std::uint64_t> selectedFunction() const noexcept;
    [[nodiscard]] const std::vector<CallGraphComponent>& components() const noexcept;
    [[nodiscard]] std::optional<std::size_t>
    componentIndexForAddress(std::uint64_t address) const noexcept;
    [[nodiscard]] std::vector<QRectF> nodeSceneBounds() const;
    [[nodiscard]] std::size_t
    displayedAssemblyLineCount(std::uint64_t address) const noexcept;
    [[nodiscard]] std::optional<QRectF>
    nodeSceneRect(std::uint64_t address) const noexcept;
    [[nodiscard]] std::vector<QPainterPath> edgeScenePaths() const;
    [[nodiscard]] QRectF viewportSceneRect() const;
    [[nodiscard]] bool activateNode(std::uint64_t address);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void rebuildScene(bool preserveSelection);
    void updateSelectionState();
    void applyZoom(qreal factor);
    void limitFitScale(qreal maximumScale);
    void notifyViewportChanged();
    [[nodiscard]] CallGraphNodeItem* nodeItemFor(QGraphicsItem* item) const noexcept;

    const CallGraph* graph_ = nullptr;
    std::unordered_map<std::uint64_t, CallGraphNodeItem*> nodeItems_;
    std::vector<CallGraphEdgeItem*> edgeItems_;
    std::vector<CallGraphComponent> components_;
    std::function<void(std::uint64_t)> nodeActivationHandler_;
    std::function<const std::vector<Instruction>*(std::uint64_t)> instructionProvider_;
    std::function<void(std::optional<std::uint64_t>)> selectionChangedHandler_;
    std::function<void()> viewportChangedHandler_;
    std::uint64_t activeFunction_ = 0;
    bool rebuildingScene_ = false;
    bool middlePanning_ = false;
    bool fitOnNextShow_ = false;
    QPoint panPosition_;
};

} // namespace decompiler
