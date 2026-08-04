#include "PseudocodeView.hpp"

#include <QAbstractTextDocumentLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>

#include <algorithm>
#include <cmath>
#include <utility>

namespace decompiler {

LineNumberArea::LineNumberArea(PseudocodeView* editor)
    : QWidget(editor)
    , editor_(editor) {
    setObjectName(QStringLiteral("pseudocodeLineNumberArea"));
}

QSize LineNumberArea::sizeHint() const {
    return QSize(editor_->lineNumberAreaWidth(), 0);
}

void LineNumberArea::paintEvent(QPaintEvent* event) {
    editor_->paintLineNumberArea(event);
}

PseudocodeView::PseudocodeView(QWidget* parent)
    : QPlainTextEdit(parent)
    , lineNumberArea_(new LineNumberArea(this)) {
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);

    connect(this, &QPlainTextEdit::blockCountChanged, this, [this](int) {
        updateLineNumberAreaWidth();
    });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& rectangle, int delta) {
        updateLineNumberArea(rectangle, delta);
    });
    updateLineNumberAreaWidth();
}

void PseudocodeView::setCallTargets(
    std::unordered_map<std::string, std::uint64_t> targets) {
    callTargets_ = std::move(targets);
}

void PseudocodeView::setCallActivationHandler(
    std::function<void(std::uint64_t)> handler) {
    callActivationHandler_ = std::move(handler);
}

bool PseudocodeView::activateCallAtCursor() {
    auto cursor = textCursor();
    if(!cursor.hasSelection()) {
        cursor.select(QTextCursor::WordUnderCursor);
    }
    const auto target = callTargets_.find(cursor.selectedText().toStdString());
    if(target == callTargets_.end() || !callActivationHandler_) {
        return false;
    }

    setTextCursor(cursor);
    callActivationHandler_(target->second);
    return true;
}

int PseudocodeView::lineNumberAreaWidth() const {
    const auto maximumLine = std::max(1, blockCount());
    auto digits = 1;
    for(auto value = maximumLine; value >= 10; value /= 10) {
        ++digits;
    }
    return 10 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void PseudocodeView::paintLineNumberArea(QPaintEvent* event) {
    QPainter painter(lineNumberArea_);
    painter.fillRect(event->rect(), palette().alternateBase());

    auto block = firstVisibleBlock();
    auto blockNumber = block.blockNumber();
    auto top = static_cast<int>(
        blockBoundingGeometry(block).translated(contentOffset()).top());
    auto bottom = top + static_cast<int>(blockBoundingRect(block).height());
    painter.setPen(palette().color(QPalette::PlaceholderText));

    while(block.isValid() && top <= event->rect().bottom()) {
        if(block.isVisible() && bottom >= event->rect().top()) {
            painter.drawText(
                0,
                top,
                lineNumberArea_->width() - 5,
                fontMetrics().height(),
                Qt::AlignRight,
                QString::number(blockNumber + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + static_cast<int>(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

void PseudocodeView::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    const auto contents = contentsRect();
    lineNumberArea_->setGeometry(
        QRect(contents.left(), contents.top(), lineNumberAreaWidth(), contents.height()));
}

void PseudocodeView::mouseDoubleClickEvent(QMouseEvent* event) {
    auto cursor = cursorForPosition(event->position().toPoint());
    cursor.select(QTextCursor::WordUnderCursor);
    setTextCursor(cursor);
    if(activateCallAtCursor()) {
        event->accept();
        return;
    }
    QPlainTextEdit::mouseDoubleClickEvent(event);
}

void PseudocodeView::updateLineNumberAreaWidth() {
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void PseudocodeView::updateLineNumberArea(
    const QRect& rectangle,
    int verticalDelta) {
    if(verticalDelta != 0) {
        lineNumberArea_->scroll(0, verticalDelta);
    } else {
        lineNumberArea_->update(
            0, rectangle.y(), lineNumberArea_->width(), rectangle.height());
    }
    if(rectangle.contains(viewport()->rect())) {
        updateLineNumberAreaWidth();
    }
}

} // namespace decompiler
