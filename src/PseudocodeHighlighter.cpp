#include "PseudocodeHighlighter.hpp"

#include <QColor>
#include <QFont>
#include <QTextDocument>

namespace decompiler {

static QTextCharFormat textFormat(const char* color, bool bold = false) {
    QTextCharFormat format;
    format.setForeground(QColor(QString::fromLatin1(color)));
    if(bold) {
        format.setFontWeight(QFont::Bold);
    }
    return format;
}

PseudocodeHighlighter::PseudocodeHighlighter(QTextDocument* document)
    : QSyntaxHighlighter(document) {
    setObjectName(QStringLiteral("pseudocodeHighlighter"));

    rules_.push_back(HighlightRule {
        .pattern = QRegularExpression(
            QStringLiteral("\\b(if|else|do|while|return|goto)\\b")),
        .format = textFormat("#7C3AED", true),
    });
    rules_.push_back(HighlightRule {
        .pattern = QRegularExpression(
            QStringLiteral("\\b(void|bool|int|long|auto|static_cast)\\b")),
        .format = textFormat("#2563EB", true),
    });
    rules_.push_back(HighlightRule {
        .pattern = QRegularExpression(QStringLiteral("\\b(0x[0-9A-Fa-f]+|[0-9]+)\\b")),
        .format = textFormat("#059669"),
    });
    rules_.push_back(HighlightRule {
        .pattern = QRegularExpression(QStringLiteral("\\bblock_[0-9A-Fa-f]+\\b")),
        .format = textFormat("#D97706"),
    });
    rules_.push_back(HighlightRule {
        .pattern = QRegularExpression(
            QStringLiteral("\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*\\()")),
        .format = textFormat("#0891B2"),
    });
    commentFormat_ = textFormat("#6B7280");
    commentFormat_.setFontItalic(true);
}

void PseudocodeHighlighter::highlightBlock(const QString& text) {
    for(const auto& rule : rules_) {
        auto matches = rule.pattern.globalMatch(text);
        while(matches.hasNext()) {
            const auto match = matches.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }

    const auto commentStart = text.indexOf(QStringLiteral("//"));
    if(commentStart >= 0) {
        setFormat(commentStart, text.size() - commentStart, commentFormat_);
    }
}

} // namespace decompiler
