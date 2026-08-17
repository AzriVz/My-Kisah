#include "PseudocodeHighlighter.hpp"
#include "PseudocodeView.hpp"

#include <QApplication>
#include <QSyntaxHighlighter>
#include <QTextDocument>

#include <cstdint>
#include <iostream>
#include <string_view>
#include <unordered_map>

static int failures = 0;

static void expect(bool condition, std::string_view message) {
    if(condition) {
        return;
    }
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    decompiler::PseudocodeView view;
    view.setPlainText(QStringLiteral(
        "int main() {\n"
        "    return helper(4);\n"
        "}\n"));
    expect(view.isReadOnly(), "pseudocode view should be read-only");
    expect(view.lineNumberAreaWidth() > 10, "line-number gutter width is invalid");
    expect(
        view.findChild<QWidget*>(QStringLiteral("pseudocodeLineNumberArea")) != nullptr,
        "line-number gutter was not created");

    decompiler::PseudocodeHighlighter highlighter(view.document());
    expect(
        view.document()->findChild<QSyntaxHighlighter*>(
            QStringLiteral("pseudocodeHighlighter")) != nullptr,
        "pseudocode syntax highlighter was not attached");

    constexpr std::uint64_t helperAddress = 0x401100;
    std::uint64_t activatedAddress = 0;
    view.setCallTargets(std::unordered_map<std::string, std::uint64_t> {
        {"helper", helperAddress},
    });
    view.setCallActivationHandler(
        [&](std::uint64_t address) { activatedAddress = address; });
    view.setTextCursor(view.document()->find(QStringLiteral("helper")));
    expect(view.activateCallAtCursor(), "known pseudocode call should activate");
    expect(activatedAddress == helperAddress, "activated call target mismatch");

    view.setTextCursor(view.document()->find(QStringLiteral("return")));
    expect(!view.activateCallAtCursor(), "non-call keyword should not navigate");
    return failures == 0 ? 0 : 1;
}
