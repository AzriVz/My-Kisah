#include "AssemblyGraphTable.hpp"

#include <QHeaderView>
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <QPolygonF>

#include <algorithm>
#include <array>
#include <limits>
#include <numeric>

namespace {

constexpr int assemblyFlowGutterWidth = 118;
constexpr std::size_t maximumFlowLanes = 12;

QColor flowColor(decompiler::InstructionKind kind) {
    using decompiler::InstructionKind;
    switch(kind) {
    case InstructionKind::ConditionalJump:
        return QColor(QStringLiteral("#2D9B50"));
    case InstructionKind::UnconditionalJump:
        return QColor(QStringLiteral("#2878C7"));
    case InstructionKind::Call:
        return QColor(QStringLiteral("#8A5AC2"));
    case InstructionKind::IndirectJump:
        return QColor(QStringLiteral("#C27824"));
    case InstructionKind::Normal:
    case InstructionKind::Return:
    case InstructionKind::Invalid:
        return QColor(QStringLiteral("#777777"));
    }
    return QColor(QStringLiteral("#777777"));
}

} // namespace

namespace decompiler {

AssemblyGraphTable::AssemblyGraphTable(QWidget* parent)
    : QTableWidget(parent) {
    setShowGrid(false);
    setAlternatingRowColors(false);
    verticalHeader()->setDefaultSectionSize(21);
    verticalHeader()->setMinimumSectionSize(18);
}

void AssemblyGraphTable::setFlowEdges(std::vector<AssemblyFlowEdge> edges) {
    flowEdges_ = std::move(edges);
    assignFlowLanes();
    viewport()->update();
}

void AssemblyGraphTable::clearFlowEdges() {
    flowEdges_.clear();
    viewport()->update();
}

std::size_t AssemblyGraphTable::flowEdgeCount() const noexcept {
    return flowEdges_.size();
}

int AssemblyGraphTable::flowGutterWidth() noexcept {
    return assemblyFlowGutterWidth;
}

void AssemblyGraphTable::paintEvent(QPaintEvent* event) {
    QTableWidget::paintEvent(event);
    if(flowEdges_.empty() || columnCount() == 0) {
        return;
    }

    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setClipRect(event->rect());

    const auto gutterLeft = columnViewportPosition(0);
    const auto gutterRight = gutterLeft + assemblyFlowGutterWidth;
    constexpr qreal laneSpacing = 8.0;
    constexpr qreal firstLaneInset = 12.0;

    for(const auto& edge : flowEdges_) {
        if(edge.sourceRow < 0 || edge.targetRow < 0
           || edge.sourceRow >= rowCount() || edge.targetRow >= rowCount()) {
            continue;
        }

        const auto sourceY = static_cast<qreal>(rowViewportPosition(edge.sourceRow))
                             + static_cast<qreal>(rowHeight(edge.sourceRow)) / 2.0;
        const auto targetY = static_cast<qreal>(rowViewportPosition(edge.targetRow))
                             + static_cast<qreal>(rowHeight(edge.targetRow)) / 2.0;
        if(std::max(sourceY, targetY) < 0.0
           || std::min(sourceY, targetY) > static_cast<qreal>(viewport()->height())) {
            continue;
        }

        const auto connectedToSelection = currentRow() == edge.sourceRow
                                          || currentRow() == edge.targetRow;
        auto color = flowColor(edge.kind);
        if(currentRow() >= 0 && !connectedToSelection) {
            color.setAlpha(115);
        }

        QPen pen(color, connectedToSelection ? 2.2 : 1.25);
        pen.setCosmetic(true);
        pen.setJoinStyle(Qt::MiterJoin);
        pen.setCapStyle(Qt::SquareCap);
        if(edge.kind == InstructionKind::Call) {
            pen.setStyle(Qt::DashLine);
        }
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);

        const auto endpointX = static_cast<qreal>(gutterRight - 3);
        const auto laneX = endpointX - firstLaneInset
                           - static_cast<qreal>(edge.lane) * laneSpacing;
        QPainterPath path;
        path.moveTo(endpointX, sourceY);
        path.lineTo(laneX, sourceY);
        path.lineTo(laneX, targetY);
        path.lineTo(endpointX - 7.0, targetY);
        painter.drawPath(path);

        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawPolygon(QPolygonF {
            QPointF(endpointX, targetY),
            QPointF(endpointX - 7.0, targetY - 4.0),
            QPointF(endpointX - 7.0, targetY + 4.0),
        });
    }
}

void AssemblyGraphTable::assignFlowLanes() {
    std::vector<std::size_t> edgeOrder(flowEdges_.size());
    std::iota(edgeOrder.begin(), edgeOrder.end(), 0);
    std::sort(edgeOrder.begin(), edgeOrder.end(), [this](std::size_t left, std::size_t right) {
        const auto leftStart = std::min(
            flowEdges_[left].sourceRow, flowEdges_[left].targetRow);
        const auto rightStart = std::min(
            flowEdges_[right].sourceRow, flowEdges_[right].targetRow);
        if(leftStart != rightStart) {
            return leftStart < rightStart;
        }
        const auto leftSpan = std::abs(
            flowEdges_[left].targetRow - flowEdges_[left].sourceRow);
        const auto rightSpan = std::abs(
            flowEdges_[right].targetRow - flowEdges_[right].sourceRow);
        return leftSpan > rightSpan;
    });

    std::array<int, maximumFlowLanes> laneEndRows;
    laneEndRows.fill(std::numeric_limits<int>::min());
    for(const auto edgeIndex : edgeOrder) {
        auto& edge = flowEdges_[edgeIndex];
        const auto intervalStart = std::min(edge.sourceRow, edge.targetRow);
        const auto intervalEnd = std::max(edge.sourceRow, edge.targetRow);
        auto selectedLane = laneEndRows.size();
        for(std::size_t lane = 0; lane < laneEndRows.size(); ++lane) {
            if(laneEndRows[lane] < intervalStart) {
                selectedLane = lane;
                break;
            }
        }
        if(selectedLane == laneEndRows.size()) {
            selectedLane = static_cast<std::size_t>(intervalStart) % laneEndRows.size();
        }
        edge.lane = selectedLane;
        laneEndRows[selectedLane] = intervalEnd;
    }
}

} // namespace decompiler
