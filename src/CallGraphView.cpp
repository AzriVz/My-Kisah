#include "CallGraphView.hpp"

#include "CallGraph.hpp"

#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QFontDatabase>
#include <QGraphicsItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QString>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <vector>
#include <utility>

static constexpr int callGraphAddressRole = 1;
static constexpr int callGraphExternalRole = 2;
static constexpr qreal nodeWidth = 176.0;
static constexpr qreal nodeHeight = 58.0;

static QString graphAddress(std::uint64_t address) {
    return QStringLiteral("0x") + QString::number(address, 16).toUpper();
}

namespace decompiler {

CallGraphView::CallGraphView(QWidget* parent)
    : QGraphicsView(parent) {
    setScene(new QGraphicsScene(this));
    setRenderHint(QPainter::Antialiasing, true);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setBackgroundBrush(QColor(QStringLiteral("#F8FAFC")));
}

void CallGraphView::setGraph(const CallGraph& graph) {
    clearGraph();
    edgeCount_ = graph.edges().size();
    if(graph.nodes().empty()) {
        return;
    }

    const auto columns = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::ceil(std::sqrt(graph.nodes().size()))));
    constexpr qreal horizontalGap = 76.0;
    constexpr qreal verticalGap = 66.0;
    std::unordered_map<std::uint64_t, QPointF> positions;
    positions.reserve(graph.nodes().size());

    for(std::size_t index = 0; index < graph.nodes().size(); ++index) {
        const auto row = index / columns;
        const auto column = index % columns;
        positions.emplace(
            graph.nodes()[index].address,
            QPointF(
                static_cast<qreal>(column) * (nodeWidth + horizontalGap),
                static_cast<qreal>(row) * (nodeHeight + verticalGap)));
    }

    QPen edgePen(QColor(QStringLiteral("#94A3B8")));
    edgePen.setWidthF(1.6);
    for(const auto& edge : graph.edges()) {
        const auto caller = positions.find(edge.callerAddress);
        const auto callee = positions.find(edge.calleeAddress);
        if(caller == positions.end() || callee == positions.end()) {
            continue;
        }
        const QPointF start = caller->second + QPointF(nodeWidth / 2.0, nodeHeight / 2.0);
        const QPointF end = callee->second + QPointF(nodeWidth / 2.0, nodeHeight / 2.0);
        if(edge.callerAddress == edge.calleeAddress) {
            auto* loop = scene()->addEllipse(
                caller->second.x() + nodeWidth - 22.0,
                caller->second.y() - 22.0,
                42.0,
                42.0,
                edgePen);
            loop->setZValue(-1.0);
        } else {
            auto* line = scene()->addLine(QLineF(start, end), edgePen);
            line->setZValue(-1.0);
        }
    }

    const auto fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    for(const auto& node : graph.nodes()) {
        const auto position = positions.at(node.address);
        auto* rectangle = scene()->addRect(QRectF(position, QSizeF(nodeWidth, nodeHeight)));
        rectangle->setData(callGraphAddressRole, static_cast<qulonglong>(node.address));
        rectangle->setData(callGraphExternalRole, node.isExternal);
        rectangle->setFlag(QGraphicsItem::ItemIsSelectable, true);
        rectangle->setToolTip(
            QStringLiteral("%1\n%2").arg(QString::fromStdString(node.name), graphAddress(node.address)));

        auto* label = new QGraphicsTextItem(rectangle);
        label->setFont(fixedFont);
        label->setPlainText(
            QStringLiteral("%1\n%2")
                .arg(QString::fromStdString(node.name), graphAddress(node.address)));
        label->setTextWidth(nodeWidth - 16.0);
        label->setPos(8.0, 5.0);
        label->setDefaultTextColor(QColor(QStringLiteral("#0F172A")));
        applyNodeStyle(rectangle, node.address == activeFunction_, node.isExternal);
        nodeItems_.emplace(node.address, rectangle);
    }

    scene()->setSceneRect(scene()->itemsBoundingRect().adjusted(-30.0, -30.0, 30.0, 30.0));
    fitInView(scene()->sceneRect(), Qt::KeepAspectRatio);
}

void CallGraphView::clearGraph() {
    scene()->clear();
    nodeItems_.clear();
    edgeCount_ = 0;
    activeFunction_ = 0;
    resetTransform();
}

void CallGraphView::setActiveFunction(std::uint64_t address) {
    const auto oldNode = nodeItems_.find(activeFunction_);
    if(oldNode != nodeItems_.end()) {
        applyNodeStyle(
            oldNode->second,
            false,
            oldNode->second->data(callGraphExternalRole).toBool());
    }

    activeFunction_ = address;
    const auto newNode = nodeItems_.find(activeFunction_);
    if(newNode != nodeItems_.end()) {
        applyNodeStyle(
            newNode->second,
            true,
            newNode->second->data(callGraphExternalRole).toBool());
    }
}

void CallGraphView::setNodeActivationHandler(
    std::function<void(std::uint64_t)> handler) {
    nodeActivationHandler_ = std::move(handler);
}

void CallGraphView::zoomIn() {
    if(zoomFactor() < 4.0) {
        scale(1.2, 1.2);
    }
}

void CallGraphView::zoomOut() {
    if(zoomFactor() > 0.2) {
        scale(1.0 / 1.2, 1.0 / 1.2);
    }
}

void CallGraphView::resetZoom() {
    resetTransform();
}

std::size_t CallGraphView::nodeCount() const noexcept {
    return nodeItems_.size();
}

std::size_t CallGraphView::edgeCount() const noexcept {
    return edgeCount_;
}

qreal CallGraphView::zoomFactor() const noexcept {
    return transform().m11();
}

std::uint64_t CallGraphView::activeFunction() const noexcept {
    return activeFunction_;
}

bool CallGraphView::activateNode(std::uint64_t address) {
    if(!nodeItems_.contains(address) || !nodeActivationHandler_) {
        return false;
    }
    nodeActivationHandler_(address);
    return true;
}

void CallGraphView::wheelEvent(QWheelEvent* event) {
    if(event->angleDelta().y() > 0) {
        zoomIn();
    } else if(event->angleDelta().y() < 0) {
        zoomOut();
    }
    event->accept();
}

void CallGraphView::mousePressEvent(QMouseEvent* event) {
    pressPosition_ = event->position().toPoint();
    QGraphicsView::mousePressEvent(event);
}

void CallGraphView::mouseReleaseEvent(QMouseEvent* event) {
    const auto releasePosition = event->position().toPoint();
    const bool isClick = (releasePosition - pressPosition_).manhattanLength()
                         <= QApplication::startDragDistance();
    QGraphicsView::mouseReleaseEvent(event);
    if(!isClick || event->button() != Qt::LeftButton) {
        return;
    }

    auto* rectangle = nodeItemFor(itemAt(releasePosition));
    if(rectangle != nullptr) {
        static_cast<void>(
            activateNode(rectangle->data(callGraphAddressRole).toULongLong()));
    }
}

void CallGraphView::applyNodeStyle(
    QGraphicsRectItem* item,
    bool active,
    bool external) {
    QPen pen(active ? QColor(QStringLiteral("#2563EB"))
                    : QColor(QStringLiteral("#64748B")));
    pen.setWidthF(active ? 3.0 : 1.4);
    if(external) {
        pen.setStyle(Qt::DashLine);
    }
    item->setPen(pen);
    item->setBrush(
        active ? QColor(QStringLiteral("#DBEAFE"))
               : external ? QColor(QStringLiteral("#E2E8F0"))
                          : QColor(QStringLiteral("#FFFFFF")));
}

QGraphicsRectItem* CallGraphView::nodeItemFor(QGraphicsItem* item) const noexcept {
    while(item != nullptr) {
        if(item->type() == QGraphicsRectItem::Type
           && item->data(callGraphAddressRole).isValid()) {
            return static_cast<QGraphicsRectItem*>(item);
        }
        item = item->parentItem();
    }
    return nullptr;
}

} // namespace decompiler
