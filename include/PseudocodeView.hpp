#pragma once

#include <QPlainTextEdit>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

class QMouseEvent;
class QPaintEvent;
class QResizeEvent;

namespace decompiler {

class PseudocodeView;

class LineNumberArea final : public QWidget {
public:
    explicit LineNumberArea(PseudocodeView* editor);

    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    PseudocodeView* editor_ = nullptr;
};

class PseudocodeView final : public QPlainTextEdit {
public:
    explicit PseudocodeView(QWidget* parent = nullptr);

    void setCallTargets(std::unordered_map<std::string, std::uint64_t> targets);
    void setCallActivationHandler(std::function<void(std::uint64_t)> handler);
    [[nodiscard]] bool activateCallAtCursor();
    [[nodiscard]] int lineNumberAreaWidth() const;
    void paintLineNumberArea(QPaintEvent* event);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect& rectangle, int verticalDelta);

    LineNumberArea* lineNumberArea_ = nullptr;
    std::unordered_map<std::string, std::uint64_t> callTargets_;
    std::function<void(std::uint64_t)> callActivationHandler_;
};

} // namespace decompiler
