#include "GraphOverviewWidget.hpp"

#include "CallGraphView.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>

#include <algorithm>

namespace decompiler {

GraphOverviewWidget::GraphOverviewWidget(QWidget* parent)
    : QWidget(parent) {
    setAutoFillBackground(false);
    setCursor(Qt::CrossCursor);
    setToolTip(tr("Click or drag to move the main graph viewport."));
}

void GraphOverviewWidget::setGraphView(CallGraphView* view) {
    graphView_ = view;
    if(graphView_ != nullptr) {
        const QPointer<GraphOverviewWidget> self(this);
        graphView_->setViewportChangedHandler([self] {
            if(self != nullptr) {
                self->update();
            }
        });
    }
    update();
}

void GraphOverviewWidget::navigateToScenePoint(const QPointF& point) {
    if(graphView_ != nullptr) {
        graphView_->centerOnScenePoint(point);
        update();
    }
}

bool GraphOverviewWidget::hasGraphData() const noexcept {
    return graphView_ != nullptr && graphView_->nodeCount() > 0;
}

QRectF GraphOverviewWidget::overviewViewportRect() const {
    if(!hasGraphData()) {
        return {};
    }
    return graphTransform().mapRect(graphView_->viewportSceneRect());
}

QSize GraphOverviewWidget::minimumSizeHint() const {
    return QSize(160, 120);
}

void GraphOverviewWidget::paintEvent(QPaintEvent* event) {
    static_cast<void>(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(QStringLiteral("#EAF3F4")));
    painter.setPen(QPen(QColor(QStringLiteral("#91A7AA")), 1.0));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    if(!hasGraphData()) {
        painter.setPen(QColor(QStringLiteral("#607477")));
        painter.drawText(rect().adjusted(8, 8, -8, -8), Qt::AlignCenter, tr("No graph overview"));
        return;
    }

    painter.save();
    painter.setTransform(graphTransform(), true);
    QPen edgePen(QColor(QStringLiteral("#5C9D68")), 1.0);
    edgePen.setCosmetic(true);
    painter.setPen(edgePen);
    painter.setBrush(Qt::NoBrush);
    for(const auto& path : graphView_->edgeScenePaths()) {
        painter.drawPath(path);
    }

    QPen nodePen(QColor(QStringLiteral("#707070")), 1.0);
    nodePen.setCosmetic(true);
    painter.setPen(nodePen);
    painter.setBrush(QColor(QStringLiteral("#E4E4E4")));
    for(const auto& nodeBounds : graphView_->nodeSceneBounds()) {
        painter.drawRect(nodeBounds);
    }

    QPen viewportPen(QColor(QStringLiteral("#176D91")), 1.8);
    viewportPen.setCosmetic(true);
    painter.setPen(viewportPen);
    painter.setBrush(QColor(45, 138, 174, 35));
    painter.drawRect(graphView_->viewportSceneRect());
    painter.restore();
}

void GraphOverviewWidget::mousePressEvent(QMouseEvent* event) {
    if(event->button() != Qt::LeftButton || !hasGraphData()) {
        QWidget::mousePressEvent(event);
        return;
    }
    draggingViewport_ = true;
    setCursor(Qt::ClosedHandCursor);
    navigateToScenePoint(scenePointAt(event->position()));
    event->accept();
}

void GraphOverviewWidget::mouseMoveEvent(QMouseEvent* event) {
    if(!draggingViewport_) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    navigateToScenePoint(scenePointAt(event->position()));
    event->accept();
}

void GraphOverviewWidget::mouseReleaseEvent(QMouseEvent* event) {
    if(event->button() == Qt::LeftButton && draggingViewport_) {
        draggingViewport_ = false;
        setCursor(Qt::CrossCursor);
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

QTransform GraphOverviewWidget::graphTransform() const {
    if(graphView_ == nullptr || graphView_->scene() == nullptr) {
        return {};
    }
    const auto source = graphView_->scene()->sceneRect();
    const QRectF target = QRectF(rect()).adjusted(8.0, 8.0, -8.0, -8.0);
    if(source.isEmpty() || target.isEmpty()) {
        return {};
    }
    const auto scale = std::min(target.width() / source.width(), target.height() / source.height());
    QTransform transform;
    transform.translate(target.center().x(), target.center().y());
    transform.scale(scale, scale);
    transform.translate(-source.center().x(), -source.center().y());
    return transform;
}

QPointF GraphOverviewWidget::scenePointAt(const QPointF& widgetPoint) const {
    bool invertible = false;
    const auto inverse = graphTransform().inverted(&invertible);
    return invertible ? inverse.map(widgetPoint) : QPointF {};
}

} // namespace decompiler
