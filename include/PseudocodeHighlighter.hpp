#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

#include <vector>

namespace decompiler {

class PseudocodeHighlighter final : public QSyntaxHighlighter {
public:
    explicit PseudocodeHighlighter(QTextDocument* document);

protected:
    void highlightBlock(const QString& text) override;

private:
    struct HighlightRule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    std::vector<HighlightRule> rules_;
    QTextCharFormat commentFormat_;
};

} // namespace decompiler
