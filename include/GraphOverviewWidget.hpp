#pragma once

#include <QPointer>
#include <QTransform>
#include <QWidget>

class QMouseEvent;
class QPaintEvent;

namespace decompiler {

class CallGraphView;

class GraphOverviewWidget final : public QWidget {
public:
    explicit GraphOverviewWidget(QWidget* parent = nullptr);

    void setGraphView(CallGraphView* view);
    void navigateToScenePoint(const QPointF& point);

    [[nodiscard]] bool hasGraphData() const noexcept;
    [[nodiscard]] QRectF overviewViewportRect() const;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    [[nodiscard]] QTransform graphTransform() const;
    [[nodiscard]] QPointF scenePointAt(const QPointF& widgetPoint) const;

    QPointer<CallGraphView> graphView_;
    bool draggingViewport_ = false;
};

} // namespace decompiler
