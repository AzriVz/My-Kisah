#include "CallGraphView.hpp"

#include "CallGraph.hpp"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPathStroker>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QStyleOptionGraphicsItem>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

static constexpr int callGraphAddressRole = 1;
static constexpr int callGraphNodeRole = 2;
static constexpr qreal callGraphNodeWidth = 328.0;
static constexpr qreal callGraphMinimumNodeHeight = 158.0;
static constexpr qreal callGraphInstructionTop = 31.0;
static constexpr qreal callGraphInstructionLineHeight = 15.5;
static constexpr qreal callGraphFooterGap = 4.5;
static constexpr qreal callGraphFooterHeight = 29.5;
static constexpr std::size_t callGraphMinimumAssemblyBodyLines = 6;
static constexpr qreal minimumGraphScale = 0.05;
static constexpr qreal maximumGraphScale = 5.0;
static constexpr qreal graphZoomStep = 1.15;

static QString graphAddress(std::uint64_t address) {
    return QStringLiteral("0x") + QString::number(address, 16).toUpper();
}

static QString graphSize(std::uint64_t size) {
    return QStringLiteral("0x") + QString::number(size, 16).toUpper();
}

static qreal graphNodeHeight(bool external, std::size_t instructionCount) {
    if(external || instructionCount <= callGraphMinimumAssemblyBodyLines) {
        return callGraphMinimumNodeHeight;
    }
    return callGraphInstructionTop
           + static_cast<qreal>(instructionCount) * callGraphInstructionLineHeight
           + callGraphFooterGap + callGraphFooterHeight;
}

namespace decompiler {

class CallGraphNodeItem final : public QGraphicsItem {
public:
    enum { Type = QGraphicsItem::UserType + 31 };

    CallGraphNodeItem(
        CallGraphNode node,
        std::size_t incoming,
        std::size_t outgoing,
        const std::vector<Instruction>* instructions)
        : node_(std::move(node))
        , nodeHeight_(graphNodeHeight(
              node_.isExternal,
              instructions == nullptr ? 0 : instructions->size()))
        , incoming_(incoming)
        , outgoing_(outgoing)
        , instructions_(instructions) {
        setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsFocusable);
        setAcceptHoverEvents(true);
        setCursor(Qt::PointingHandCursor);
        setData(callGraphAddressRole, static_cast<qulonglong>(node_.address));
        setData(callGraphNodeRole, true);
        setZValue(2.0);
        setToolTip(
            QStringLiteral(
                "%1\nAddress: %2\nInstructions: %3\nIncoming: %4\nOutgoing: %5\nSize: %6\nStatus: %7")
                .arg(
                    QString::fromStdString(node_.name),
                    graphAddress(node_.address),
                    QString::number(instructions_ == nullptr ? 0 : instructions_->size()),
                    QString::number(incoming_),
                    QString::number(outgoing_),
                    graphSize(node_.size),
                    node_.isExternal ? QStringLiteral("external / unresolved")
                                     : QStringLiteral("internal")));
    }

    [[nodiscard]] int type() const override {
        return Type;
    }

    [[nodiscard]] QRectF boundingRect() const override {
        return QRectF(0.0, 0.0, callGraphNodeWidth, nodeHeight_);
    }

    [[nodiscard]] const CallGraphNode& node() const noexcept {
        return node_;
    }

    [[nodiscard]] std::size_t displayedAssemblyLineCount() const noexcept {
        return node_.isExternal || instructions_ == nullptr
                   ? 0
                   : instructions_->size();
    }

    [[nodiscard]] QPointF inputAnchor() const {
        return mapToScene(QPointF(callGraphNodeWidth / 2.0, 0.0));
    }

    [[nodiscard]] QPointF outputAnchor() const {
        return mapToScene(QPointF(callGraphNodeWidth / 2.0, nodeHeight_));
    }

    void paint(
        QPainter* painter,
        const QStyleOptionGraphicsItem*,
        QWidget*) override {
        const bool selected = isSelected();
        QColor border(QStringLiteral("#707070"));
        QColor header(QStringLiteral("#E2E2E2"));
        QColor body(QStringLiteral("#F8F8F8"));
        if(hovered_) {
            border = QColor(QStringLiteral("#4E4E4E"));
            header = QColor(QStringLiteral("#DCDCDC"));
            body = QColor(QStringLiteral("#F4F4F4"));
        }
        if(selected) {
            border = QColor(QStringLiteral("#252525"));
            header = QColor(QStringLiteral("#D2D2D2"));
            body = QColor(QStringLiteral("#F1F1F1"));
        }

        QPen borderPen(border, selected ? 2.2 : 1.0);
        if(node_.isExternal) {
            borderPen.setStyle(Qt::DashLine);
        }
        painter->setPen(borderPen);
        painter->setBrush(body);
        painter->drawRect(boundingRect());

        constexpr qreal headerHeight = 27.0;
        painter->fillRect(QRectF(0.5, 0.5, callGraphNodeWidth - 1.0, headerHeight), header);
        painter->setPen(QPen(border, 0.8));
        painter->drawLine(QPointF(0.5, headerHeight), QPointF(callGraphNodeWidth - 0.5, headerHeight));

        auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setPointSizeF(8.5);
        font.setBold(true);
        painter->setFont(font);
        painter->setPen(QColor(QStringLiteral("#202020")));
        const QFontMetricsF headerMetrics(font);
        const auto addressText = graphAddress(node_.address);
        const auto addressWidth = headerMetrics.horizontalAdvance(addressText);
        const QRectF nameRect(7.0, 3.0, callGraphNodeWidth - addressWidth - 21.0, 21.0);
        const auto name = node_.name.empty()
                              ? QStringLiteral("sub_%1").arg(QString::number(node_.address, 16))
                              : QString::fromStdString(node_.name);
        painter->drawText(
            nameRect,
            Qt::AlignLeft | Qt::AlignVCenter,
            headerMetrics.elidedText(name, Qt::ElideRight, nameRect.width()));
        painter->drawText(
            QRectF(callGraphNodeWidth - addressWidth - 7.0, 3.0, addressWidth, 21.0),
            Qt::AlignRight | Qt::AlignVCenter,
            addressText);

        font.setBold(false);
        font.setPointSizeF(7.7);
        painter->setFont(font);
        const QFontMetricsF instructionMetrics(font);
        constexpr qreal instructionAddressWidth = 79.0;
        constexpr qreal instructionMnemonicWidth = 61.0;
        const auto footerTop = nodeHeight_ - callGraphFooterHeight;
        const auto emptyMessageHeight = footerTop - callGraphInstructionTop
                                        - callGraphFooterGap;

        if(node_.isExternal) {
            painter->setPen(QColor(QStringLiteral("#666666")));
            painter->drawText(
                QRectF(
                    7.0,
                    callGraphInstructionTop,
                    callGraphNodeWidth - 14.0,
                    emptyMessageHeight),
                Qt::AlignCenter,
                QStringLiteral("<external target - no local assembly>"));
        } else if(instructions_ == nullptr || instructions_->empty()) {
            painter->setPen(QColor(QStringLiteral("#666666")));
            painter->drawText(
                QRectF(
                    7.0,
                    callGraphInstructionTop,
                    callGraphNodeWidth - 14.0,
                    emptyMessageHeight),
                Qt::AlignCenter,
                QStringLiteral("<no decoded instructions>"));
        } else {
            for(std::size_t index = 0; index < displayedAssemblyLineCount(); ++index) {
                const auto& instruction = instructions_->at(index);
                const auto top = callGraphInstructionTop
                                 + static_cast<qreal>(index)
                                       * callGraphInstructionLineHeight;
                painter->setPen(QColor(QStringLiteral("#666666")));
                painter->drawText(
                    QRectF(
                        7.0,
                        top,
                        instructionAddressWidth,
                        callGraphInstructionLineHeight),
                    Qt::AlignLeft | Qt::AlignVCenter,
                    graphAddress(instruction.address));

                font.setBold(true);
                painter->setFont(font);
                painter->setPen(QColor(QStringLiteral("#202020")));
                const QRectF mnemonicRect(
                    88.0,
                    top,
                    instructionMnemonicWidth,
                    callGraphInstructionLineHeight);
                painter->drawText(
                    mnemonicRect,
                    Qt::AlignLeft | Qt::AlignVCenter,
                    instructionMetrics.elidedText(
                        QString::fromStdString(instruction.mnemonic),
                        Qt::ElideRight,
                        mnemonicRect.width()));

                font.setBold(false);
                painter->setFont(font);
                const QRectF operandRect(
                    151.0,
                    top,
                    callGraphNodeWidth - 158.0,
                    callGraphInstructionLineHeight);
                painter->drawText(
                    operandRect,
                    Qt::AlignLeft | Qt::AlignVCenter,
                    instructionMetrics.elidedText(
                        QString::fromStdString(instruction.operandText),
                        Qt::ElideRight,
                        operandRect.width()));
            }
        }

        painter->setPen(QPen(QColor(QStringLiteral("#B0B0B0")), 0.8));
        painter->drawLine(
            QPointF(0.5, footerTop),
            QPointF(callGraphNodeWidth - 0.5, footerTop));
        font.setBold(false);
        font.setPointSizeF(7.4);
        painter->setFont(font);
        painter->setPen(QColor(QStringLiteral("#505050")));
        const auto instructionCount = instructions_ == nullptr ? 0 : instructions_->size();
        const auto footer = node_.isExternal
                                ? QStringLiteral("external | in %1 | out %2")
                                      .arg(incoming_)
                                      .arg(outgoing_)
                                : QStringLiteral("%1 instructions | in %2 | out %3 | size %4")
                                      .arg(instructionCount)
                                      .arg(incoming_)
                                      .arg(outgoing_)
                                      .arg(graphSize(node_.size));
        painter->drawText(
            QRectF(7.0, footerTop + 1.0, callGraphNodeWidth - 14.0, 25.0),
            Qt::AlignLeft | Qt::AlignVCenter,
            footer);
    }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override {
        if(change == QGraphicsItem::ItemSelectedHasChanged) {
            setZValue(value.toBool() ? 8.0 : 2.0);
            update();
        }
        return QGraphicsItem::itemChange(change, value);
    }

    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override {
        hovered_ = true;
        update();
        QGraphicsItem::hoverEnterEvent(event);
    }

    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override {
        hovered_ = false;
        update();
        QGraphicsItem::hoverLeaveEvent(event);
    }

private:
    CallGraphNode node_;
    qreal nodeHeight_ = callGraphMinimumNodeHeight;
    std::size_t incoming_ = 0;
    std::size_t outgoing_ = 0;
    const std::vector<Instruction>* instructions_ = nullptr;
    bool hovered_ = false;
};

class CallGraphEdgeItem final : public QGraphicsItem {
public:
    enum { Type = QGraphicsItem::UserType + 32 };

    CallGraphEdgeItem(
        CallGraphNodeItem* source,
        CallGraphNodeItem* target,
        bool external,
        std::size_t routeIndex)
        : source_(source)
        , target_(target)
        , external_(external)
        , recursive_(source == target) {
        setZValue(-4.0);
        setAcceptedMouseButtons(Qt::NoButton);
        buildPath(routeIndex);
    }

    [[nodiscard]] int type() const override {
        return Type;
    }

    [[nodiscard]] QRectF boundingRect() const override {
        return bounds_;
    }

    [[nodiscard]] QPainterPath scenePath() const {
        return mapToScene(path_);
    }

    [[nodiscard]] bool connects(std::uint64_t address) const noexcept {
        return source_->node().address == address || target_->node().address == address;
    }

    void setSelectionContext(bool hasSelection, bool connected) {
        const auto nextState = !hasSelection ? 0 : connected ? 1 : 2;
        if(selectionState_ != nextState) {
            selectionState_ = nextState;
            update();
        }
    }

    void paint(
        QPainter* painter,
        const QStyleOptionGraphicsItem*,
        QWidget*) override {
        QColor color = external_ ? QColor(QStringLiteral("#6D7B85"))
                                 : QColor(QStringLiteral("#3B9B52"));
        if(recursive_) {
            color = QColor(QStringLiteral("#B56B32"));
        }
        if(selectionState_ == 1) {
            color = external_ ? QColor(QStringLiteral("#3E6F86"))
                              : QColor(QStringLiteral("#157F3B"));
        } else if(selectionState_ == 2) {
            color.setAlpha(70);
        }

        QPen pen(color, selectionState_ == 1 ? 2.4 : 1.35);
        pen.setJoinStyle(Qt::MiterJoin);
        pen.setCapStyle(Qt::SquareCap);
        if(recursive_) {
            pen.setStyle(Qt::DashLine);
        }
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path_);
        painter->setPen(Qt::NoPen);
        painter->setBrush(color);
        painter->drawPolygon(arrowHead_);
    }

private:
    void buildPath(std::size_t routeIndex) {
        const auto sourceRect = source_->sceneBoundingRect();
        const auto targetRect = target_->sceneBoundingRect();
        const auto start = source_->outputAnchor();
        const auto end = target_->inputAnchor();
        const auto laneOffset = static_cast<qreal>(routeIndex % 5) * 7.0;

        path_ = QPainterPath {};
        path_.moveTo(start);
        if(recursive_) {
            const auto right = sourceRect.right() + 42.0 + laneOffset;
            const auto above = sourceRect.top() - 28.0 - laneOffset;
            path_.lineTo(start.x(), sourceRect.bottom() + 25.0);
            path_.lineTo(right, sourceRect.bottom() + 25.0);
            path_.lineTo(right, above);
            path_.lineTo(end.x(), above);
            path_.lineTo(end);
        } else if(end.y() > start.y() + 22.0) {
            const auto middleY = start.y() + (end.y() - start.y()) / 2.0 + laneOffset;
            path_.lineTo(start.x(), middleY);
            path_.lineTo(end.x(), middleY);
            path_.lineTo(end);
        } else {
            const auto right = std::max(sourceRect.right(), targetRect.right())
                               + 38.0 + laneOffset;
            const auto above = targetRect.top() - 28.0 - laneOffset;
            path_.lineTo(start.x(), start.y() + 24.0);
            path_.lineTo(right, start.y() + 24.0);
            path_.lineTo(right, above);
            path_.lineTo(end.x(), above);
            path_.lineTo(end);
        }

        const auto elementCount = path_.elementCount();
        const auto last = path_.elementAt(elementCount - 1);
        const auto previous = path_.elementAt(elementCount - 2);
        const QPointF endPoint(last.x, last.y);
        QPointF direction(endPoint.x() - previous.x, endPoint.y() - previous.y);
        const auto length = std::hypot(direction.x(), direction.y());
        if(length > 0.0) {
            direction /= length;
        } else {
            direction = QPointF(0.0, 1.0);
        }
        const QPointF normal(-direction.y(), direction.x());
        constexpr qreal arrowLength = 9.0;
        constexpr qreal arrowWidth = 4.5;
        const auto base = endPoint - direction * arrowLength;
        arrowHead_ = QPolygonF {
            endPoint,
            base + normal * arrowWidth,
            base - normal * arrowWidth,
        };

        bounds_ = path_.boundingRect().united(arrowHead_.boundingRect()).adjusted(-5.0, -5.0, 5.0, 5.0);
    }

    CallGraphNodeItem* source_ = nullptr;
    CallGraphNodeItem* target_ = nullptr;
    bool external_ = false;
    bool recursive_ = false;
    int selectionState_ = 0;
    QPainterPath path_;
    QPolygonF arrowHead_;
    QRectF bounds_;
};

CallGraphView::CallGraphView(QWidget* parent)
    : QGraphicsView(parent) {
    setScene(new QGraphicsScene(this));
    setRenderHint(QPainter::Antialiasing, true);
    setRenderHint(QPainter::TextAntialiasing, true);
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setBackgroundBrush(QColor(QStringLiteral("#F5F5F5")));
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setFrameShape(QFrame::StyledPanel);
    setFocusPolicy(Qt::StrongFocus);
    scene()->setItemIndexMethod(QGraphicsScene::BspTreeIndex);
    connect(scene(), &QGraphicsScene::selectionChanged, this, [this] {
        updateSelectionState();
    });
}

CallGraphView::~CallGraphView() {
    viewportChangedHandler_ = {};
    selectionChangedHandler_ = {};
    if(scene() != nullptr) {
        disconnect(scene(), nullptr, this, nullptr);
    }
}

void CallGraphView::setGraph(const CallGraph& graph) {
    graph_ = &graph;
    rebuildScene(false);
    fitOnNextShow_ = true;
    if(isVisible()) {
        fitOnNextShow_ = false;
        fitAll();
    }
}

void CallGraphView::clearGraph() {
    rebuildingScene_ = true;
    fitOnNextShow_ = false;
    graph_ = nullptr;
    scene()->clear();
    nodeItems_.clear();
    edgeItems_.clear();
    components_.clear();
    activeFunction_ = 0;
    resetTransform();
    auto* message = scene()->addText(tr("No call graph data available."));
    message->setDefaultTextColor(QColor(QStringLiteral("#555555")));
    message->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    message->setPos(24.0, 24.0);
    scene()->setSceneRect(message->boundingRect().translated(message->pos()).adjusted(-20.0, -20.0, 40.0, 40.0));
    rebuildingScene_ = false;
    notifyViewportChanged();
}

void CallGraphView::refreshLayout() {
    if(graph_ == nullptr) {
        clearGraph();
        return;
    }
    rebuildScene(true);
    fitAll();
}

void CallGraphView::setActiveFunction(std::uint64_t address) {
    activeFunction_ = address;
    const auto node = nodeItems_.find(address);
    if(node == nodeItems_.end()) {
        return;
    }

    const QSignalBlocker blocker(scene());
    scene()->clearSelection();
    node->second->setSelected(true);
    updateSelectionState();
    ensureVisible(node->second, 32, 32);
}

void CallGraphView::setNodeActivationHandler(
    std::function<void(std::uint64_t)> handler) {
    nodeActivationHandler_ = std::move(handler);
}

void CallGraphView::setInstructionProvider(
    std::function<const std::vector<Instruction>*(std::uint64_t)> provider) {
    instructionProvider_ = std::move(provider);
    if(graph_ != nullptr) {
        rebuildScene(true);
    }
}

void CallGraphView::setSelectionChangedHandler(
    std::function<void(std::optional<std::uint64_t>)> handler) {
    selectionChangedHandler_ = std::move(handler);
}

void CallGraphView::setViewportChangedHandler(std::function<void()> handler) {
    viewportChangedHandler_ = std::move(handler);
}

void CallGraphView::zoomIn() {
    applyZoom(graphZoomStep);
}

void CallGraphView::zoomOut() {
    applyZoom(1.0 / graphZoomStep);
}

void CallGraphView::resetZoom() {
    resetTransform();
    notifyViewportChanged();
}

void CallGraphView::fitAll() {
    if(nodeItems_.empty()) {
        resetZoom();
        return;
    }
    fitInView(scene()->sceneRect(), Qt::KeepAspectRatio);
    limitFitScale(1.0);
    notifyViewportChanged();
}

void CallGraphView::fitSelection() {
    const auto selected = selectedFunction();
    if(!selected) {
        return;
    }
    const auto node = nodeItems_.find(*selected);
    if(node != nodeItems_.end()) {
        fitInView(node->second->sceneBoundingRect().adjusted(-70.0, -70.0, 70.0, 70.0), Qt::KeepAspectRatio);
        limitFitScale(2.5);
        notifyViewportChanged();
    }
}

bool CallGraphView::fitComponent(std::size_t componentIndex) {
    if(componentIndex >= components_.size()) {
        return false;
    }
    fitInView(components_[componentIndex].sceneBounds, Qt::KeepAspectRatio);
    limitFitScale(2.5);
    notifyViewportChanged();
    return true;
}

bool CallGraphView::centerOnNode(std::uint64_t address) {
    const auto node = nodeItems_.find(address);
    if(node == nodeItems_.end()) {
        return false;
    }
    centerOn(node->second);
    notifyViewportChanged();
    return true;
}

void CallGraphView::centerOnScenePoint(const QPointF& point) {
    centerOn(point);
    notifyViewportChanged();
}

std::size_t CallGraphView::nodeCount() const noexcept {
    return nodeItems_.size();
}

std::size_t CallGraphView::edgeCount() const noexcept {
    return edgeItems_.size();
}

qreal CallGraphView::zoomFactor() const noexcept {
    return transform().m11();
}

std::uint64_t CallGraphView::activeFunction() const noexcept {
    return activeFunction_;
}

std::optional<std::uint64_t> CallGraphView::selectedFunction() const noexcept {
    for(auto* item : scene()->selectedItems()) {
        if(const auto* node = nodeItemFor(item)) {
            return node->node().address;
        }
    }
    return std::nullopt;
}

const std::vector<CallGraphComponent>& CallGraphView::components() const noexcept {
    return components_;
}

std::optional<std::size_t>
CallGraphView::componentIndexForAddress(std::uint64_t address) const noexcept {
    for(std::size_t index = 0; index < components_.size(); ++index) {
        const auto& addresses = components_[index].nodeAddresses;
        if(std::find(addresses.begin(), addresses.end(), address) != addresses.end()) {
            return index;
        }
    }
    return std::nullopt;
}

std::vector<QRectF> CallGraphView::nodeSceneBounds() const {
    std::vector<QRectF> result;
    result.reserve(nodeItems_.size());
    for(const auto& [address, item] : nodeItems_) {
        static_cast<void>(address);
        result.push_back(item->sceneBoundingRect());
    }
    return result;
}

std::size_t
CallGraphView::displayedAssemblyLineCount(std::uint64_t address) const noexcept {
    const auto node = nodeItems_.find(address);
    return node == nodeItems_.end() ? 0 : node->second->displayedAssemblyLineCount();
}

std::optional<QRectF>
CallGraphView::nodeSceneRect(std::uint64_t address) const noexcept {
    const auto node = nodeItems_.find(address);
    if(node == nodeItems_.end()) {
        return std::nullopt;
    }
    return node->second->sceneBoundingRect();
}

std::vector<QPainterPath> CallGraphView::edgeScenePaths() const {
    std::vector<QPainterPath> result;
    result.reserve(edgeItems_.size());
    for(const auto* edge : edgeItems_) {
        result.push_back(edge->scenePath());
    }
    return result;
}

QRectF CallGraphView::viewportSceneRect() const {
    return mapToScene(viewport()->rect()).boundingRect();
}

bool CallGraphView::activateNode(std::uint64_t address) {
    if(!nodeItems_.contains(address) || !nodeActivationHandler_) {
        return false;
    }
    nodeActivationHandler_(address);
    return true;
}

void CallGraphView::wheelEvent(QWheelEvent* event) {
    if((event->modifiers() & Qt::ControlModifier) == 0) {
        QGraphicsView::wheelEvent(event);
        notifyViewportChanged();
        return;
    }
    applyZoom(event->angleDelta().y() >= 0 ? graphZoomStep : 1.0 / graphZoomStep);
    event->accept();
}

void CallGraphView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    if(fitOnNextShow_ && isVisible()) {
        fitOnNextShow_ = false;
        fitAll();
        return;
    }
    notifyViewportChanged();
}

void CallGraphView::showEvent(QShowEvent* event) {
    QGraphicsView::showEvent(event);
    if(fitOnNextShow_) {
        fitOnNextShow_ = false;
        fitAll();
    }
}

void CallGraphView::scrollContentsBy(int dx, int dy) {
    QGraphicsView::scrollContentsBy(dx, dy);
    notifyViewportChanged();
}

void CallGraphView::mousePressEvent(QMouseEvent* event) {
    if(event->button() == Qt::MiddleButton) {
        middlePanning_ = true;
        panPosition_ = event->position().toPoint();
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if(event->button() == Qt::LeftButton) {
        auto* node = nodeItemFor(itemAt(event->position().toPoint()));
        scene()->clearSelection();
        if(node != nullptr) {
            node->setSelected(true);
            node->setFocus();
        }
    }
    QGraphicsView::mousePressEvent(event);
}

void CallGraphView::mouseMoveEvent(QMouseEvent* event) {
    if(middlePanning_) {
        const auto position = event->position().toPoint();
        const auto delta = position - panPosition_;
        panPosition_ = position;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void CallGraphView::mouseReleaseEvent(QMouseEvent* event) {
    if(event->button() == Qt::MiddleButton && middlePanning_) {
        middlePanning_ = false;
        viewport()->unsetCursor();
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void CallGraphView::mouseDoubleClickEvent(QMouseEvent* event) {
    if(event->button() == Qt::LeftButton) {
        if(auto* node = nodeItemFor(itemAt(event->position().toPoint()))) {
            static_cast<void>(activateNode(node->node().address));
            event->accept();
            return;
        }
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void CallGraphView::contextMenuEvent(QContextMenuEvent* event) {
    auto* node = nodeItemFor(itemAt(event->pos()));
    if(node == nullptr) {
        QGraphicsView::contextMenuEvent(event);
        return;
    }

    scene()->clearSelection();
    node->setSelected(true);
    QMenu menu(this);
    auto* openAction = menu.addAction(tr("Open Function"));
    auto* fitAction = menu.addAction(tr("Fit Node"));
    auto* centerAction = menu.addAction(tr("Center Here"));
    menu.addSeparator();
    auto* copyNameAction = menu.addAction(tr("Copy Function Name"));
    auto* copyAddressAction = menu.addAction(tr("Copy Address"));
    const auto* chosen = menu.exec(event->globalPos());
    if(chosen == openAction) {
        static_cast<void>(activateNode(node->node().address));
    } else if(chosen == fitAction) {
        fitSelection();
    } else if(chosen == centerAction) {
        centerOn(node);
        notifyViewportChanged();
    } else if(chosen == copyNameAction) {
        QApplication::clipboard()->setText(QString::fromStdString(node->node().name));
    } else if(chosen == copyAddressAction) {
        QApplication::clipboard()->setText(graphAddress(node->node().address));
    }
}

void CallGraphView::rebuildScene(bool preserveSelection) {
    const auto previouslySelected = preserveSelection ? selectedFunction() : std::nullopt;
    rebuildingScene_ = true;
    scene()->clear();
    nodeItems_.clear();
    edgeItems_.clear();
    components_.clear();

    if(graph_ == nullptr || graph_->nodes().empty()) {
        auto* message = scene()->addText(tr("No call graph data available."));
        message->setDefaultTextColor(QColor(QStringLiteral("#555555")));
        message->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        message->setPos(24.0, 24.0);
        scene()->setSceneRect(
            message->boundingRect().translated(message->pos()).adjusted(-20.0, -20.0, 40.0, 40.0));
        rebuildingScene_ = false;
        notifyViewportChanged();
        return;
    }

    std::unordered_map<std::uint64_t, std::size_t> incoming;
    std::unordered_map<std::uint64_t, std::size_t> outgoing;
    incoming.reserve(graph_->nodes().size());
    outgoing.reserve(graph_->nodes().size());
    for(const auto& node : graph_->nodes()) {
        incoming.emplace(node.address, 0);
        outgoing.emplace(node.address, 0);
    }
    for(const auto& edge : graph_->edges()) {
        ++incoming[edge.calleeAddress];
        ++outgoing[edge.callerAddress];
    }

    std::unordered_map<std::uint64_t, const std::vector<Instruction>*> nodeInstructions;
    std::unordered_map<std::uint64_t, QSizeF> nodeSizes;
    nodeInstructions.reserve(graph_->nodes().size());
    nodeSizes.reserve(graph_->nodes().size());
    for(const auto& node : graph_->nodes()) {
        const auto* instructions = instructionProvider_ == nullptr
                                       ? nullptr
                                       : instructionProvider_(node.address);
        nodeInstructions.emplace(node.address, instructions);
        nodeSizes.emplace(
            node.address,
            QSizeF(
                callGraphNodeWidth,
                graphNodeHeight(
                    node.isExternal,
                    instructions == nullptr ? 0 : instructions->size())));
    }

    const CallGraphLayoutEngine layoutEngine;
    auto layout = layoutEngine.layout(
        *graph_,
        nodeSizes,
        QSizeF(callGraphNodeWidth, callGraphMinimumNodeHeight));
    components_ = std::move(layout.components);
    nodeItems_.reserve(graph_->nodes().size());
    for(const auto& node : graph_->nodes()) {
        const auto* instructions = nodeInstructions.at(node.address);
        auto* item = new CallGraphNodeItem(
            node,
            incoming[node.address],
            outgoing[node.address],
            instructions);
        const auto position = layout.nodePositions.find(node.address);
        if(position != layout.nodePositions.end()) {
            item->setPos(position->second);
        }
        scene()->addItem(item);
        nodeItems_.emplace(node.address, item);
    }

    edgeItems_.reserve(graph_->edges().size());
    std::size_t routeIndex = 0;
    for(const auto& edge : graph_->edges()) {
        const auto source = nodeItems_.find(edge.callerAddress);
        const auto target = nodeItems_.find(edge.calleeAddress);
        if(source == nodeItems_.end() || target == nodeItems_.end()) {
            continue;
        }
        auto* edgeItem = new CallGraphEdgeItem(
            source->second,
            target->second,
            target->second->node().isExternal,
            routeIndex++);
        scene()->addItem(edgeItem);
        edgeItems_.push_back(edgeItem);
    }

    scene()->setSceneRect(scene()->itemsBoundingRect().adjusted(-55.0, -55.0, 55.0, 55.0));
    const auto selectionToRestore = previouslySelected.value_or(activeFunction_);
    if(selectionToRestore != 0) {
        const auto selectedItem = nodeItems_.find(selectionToRestore);
        if(selectedItem != nodeItems_.end()) {
            selectedItem->second->setSelected(true);
        }
    }
    rebuildingScene_ = false;
    updateSelectionState();
    notifyViewportChanged();
}

void CallGraphView::updateSelectionState() {
    const auto selected = selectedFunction();
    for(auto* edge : edgeItems_) {
        edge->setSelectionContext(selected.has_value(), selected && edge->connects(*selected));
    }
    if(!rebuildingScene_ && selectionChangedHandler_) {
        selectionChangedHandler_(selected);
    }
    viewport()->update();
}

void CallGraphView::applyZoom(qreal factor) {
    const auto currentScale = zoomFactor();
    if(currentScale <= 0.0) {
        return;
    }
    const auto targetScale = std::clamp(
        currentScale * factor, minimumGraphScale, maximumGraphScale);
    if(qFuzzyCompare(currentScale, targetScale)) {
        return;
    }
    scale(targetScale / currentScale, targetScale / currentScale);
    notifyViewportChanged();
}

void CallGraphView::limitFitScale(qreal maximumScale) {
    const auto currentScale = zoomFactor();
    if(currentScale <= 0.0) {
        return;
    }
    const auto targetScale = std::clamp(
        currentScale, minimumGraphScale, maximumScale);
    if(!qFuzzyCompare(currentScale, targetScale)) {
        scale(targetScale / currentScale, targetScale / currentScale);
    }
}

void CallGraphView::notifyViewportChanged() {
    if(viewportChangedHandler_) {
        viewportChangedHandler_();
    }
}

CallGraphNodeItem* CallGraphView::nodeItemFor(QGraphicsItem* item) const noexcept {
    while(item != nullptr) {
        if(item->type() == CallGraphNodeItem::Type
           && item->data(callGraphNodeRole).toBool()) {
            return static_cast<CallGraphNodeItem*>(item);
        }
        item = item->parentItem();
    }
    return nullptr;
}

} // namespace decompiler
