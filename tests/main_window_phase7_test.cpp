#include "AnalysisSession.hpp"
#include "Instruction.hpp"
#include "MainWindow.hpp"
#include "PseudocodeView.hpp"

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QStatusBar>
#include <QSyntaxHighlighter>
#include <QTableWidget>
#include <QTextDocument>

#include <algorithm>
#include <filesystem>
#include <iostream>

static bool currentFunctionContains(QListWidget* list, const QString& name) {
    return list->currentItem() != nullptr && list->currentItem()->text().contains(name);
}

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    if(argc < 3) {
        std::cerr << "Branching and arithmetic sample paths are required\n";
        return 1;
    }

    decompiler::MainWindow mainWindow;
    if(!mainWindow.loadBinary(argv[1])) {
        std::cerr << "MainWindow could not analyze the branching sample\n";
        return 1;
    }

    auto* functionList = mainWindow.findChild<QListWidget*>(QStringLiteral("functionList"));
    auto* pseudocode = dynamic_cast<decompiler::PseudocodeView*>(
        mainWindow.findChild<QPlainTextEdit*>(QStringLiteral("pseudocodeView")));
    auto* pseudocodeSearch =
        mainWindow.findChild<QLineEdit*>(QStringLiteral("pseudocodeSearch"));
    auto* assembly = mainWindow.findChild<QTableWidget*>(QStringLiteral("assemblyTable"));
    auto* backAction = mainWindow.findChild<QAction*>(QStringLiteral("backAction"));
    auto* forwardAction = mainWindow.findChild<QAction*>(QStringLiteral("forwardAction"));
    auto* progress = mainWindow.findChild<QProgressBar*>(QStringLiteral("analysisProgress"));
    if(functionList == nullptr || pseudocode == nullptr || pseudocodeSearch == nullptr
       || assembly == nullptr || backAction == nullptr || forwardAction == nullptr
       || progress == nullptr) {
        std::cerr << "Phase 7 widgets and actions were not created\n";
        return 1;
    }
    if(pseudocode->lineNumberAreaWidth() <= 10
       || pseudocode->findChild<QWidget*>(QStringLiteral("pseudocodeLineNumberArea")) == nullptr
       || pseudocode->document()->findChild<QSyntaxHighlighter*>(
              QStringLiteral("pseudocodeHighlighter")) == nullptr) {
        std::cerr << "Line numbers or syntax highlighting are unavailable\n";
        return 1;
    }
    if(!progress->isHidden() || backAction->isEnabled() || forwardAction->isEnabled()) {
        std::cerr << "Initial navigation or loading state is incorrect\n";
        return 1;
    }
    if(!currentFunctionContains(functionList, QStringLiteral("main"))) {
        std::cerr << "main should be selected after loading\n";
        return 1;
    }

    const auto callCursor = pseudocode->document()->find(QStringLiteral("_Z6choosei"));
    if(callCursor.isNull()) {
        std::cerr << "main pseudocode does not contain the choose call\n";
        return 1;
    }
    pseudocode->setTextCursor(callCursor);
    if(!pseudocode->activateCallAtCursor()
       || !currentFunctionContains(functionList, QStringLiteral("choose"))) {
        std::cerr << "Pseudocode call navigation did not select choose\n";
        return 1;
    }
    if(!backAction->isEnabled() || forwardAction->isEnabled()) {
        std::cerr << "Navigation history was not updated after pseudocode navigation\n";
        return 1;
    }

    backAction->trigger();
    if(!currentFunctionContains(functionList, QStringLiteral("main"))
       || !forwardAction->isEnabled()) {
        std::cerr << "Back navigation did not restore main\n";
        return 1;
    }
    forwardAction->trigger();
    if(!currentFunctionContains(functionList, QStringLiteral("choose"))) {
        std::cerr << "Forward navigation did not restore choose\n";
        return 1;
    }

    pseudocodeSearch->setText(QStringLiteral("return"));
    QCoreApplication::processEvents();
    if(pseudocode->textCursor().selectedText() != QStringLiteral("return")) {
        std::cerr << "Pseudocode text search did not select a match\n";
        return 1;
    }

    backAction->trigger();
    const auto& branchSession = mainWindow.analysisSession();
    const auto mainFunction = std::find_if(
        branchSession.functions().begin(), branchSession.functions().end(), [](const auto& function) {
            return function.name == "main";
        });
    if(mainFunction == branchSession.functions().end()) {
        std::cerr << "main was not discovered in branching sample\n";
        return 1;
    }
    const auto* mainInstructions = branchSession.instructionsFor(mainFunction->address);
    if(mainInstructions == nullptr) {
        std::cerr << "main assembly cache is unavailable\n";
        return 1;
    }
    const auto directCall = std::find_if(
        mainInstructions->begin(), mainInstructions->end(), [](const auto& instruction) {
            return instruction.kind == decompiler::InstructionKind::Call
                   && instruction.directTarget;
        });
    if(directCall == mainInstructions->end()) {
        std::cerr << "main has no direct call for assembly navigation\n";
        return 1;
    }
    assembly->cellDoubleClicked(
        static_cast<int>(std::distance(mainInstructions->begin(), directCall)), 0);
    if(!currentFunctionContains(functionList, QStringLiteral("choose"))
       || !backAction->isEnabled()) {
        std::cerr << "Assembly navigation was not added to shared history\n";
        return 1;
    }

    if(!mainWindow.loadBinary(argv[2])) {
        std::cerr << "Opening a second binary without restart failed\n";
        return 1;
    }
    if(!currentFunctionContains(functionList, QStringLiteral("main"))
       || backAction->isEnabled() || forwardAction->isEnabled()
       || !pseudocodeSearch->text().isEmpty() || !progress->isHidden()) {
        std::cerr << "Second-binary state was not reset correctly\n";
        return 1;
    }
    const auto& arithmeticSession = mainWindow.analysisSession();
    const auto arithmeticMain = std::find_if(
        arithmeticSession.functions().begin(),
        arithmeticSession.functions().end(),
        [](const auto& function) { return function.name == "main"; });
    if(arithmeticMain == arithmeticSession.functions().end()) {
        std::cerr << "main was not discovered in arithmetic sample\n";
        return 1;
    }
    const auto* firstPseudocode =
        arithmeticSession.pseudocodeFor(arithmeticMain->address);
    const auto* secondPseudocode =
        arithmeticSession.pseudocodeFor(arithmeticMain->address);
    if(firstPseudocode == nullptr || firstPseudocode != secondPseudocode) {
        std::cerr << "Pseudocode cache should return a stable entry\n";
        return 1;
    }

    const auto missingPath = std::filesystem::path("/tmp/my-kisah-file-that-does-not-exist");
    if(mainWindow.loadBinary(missingPath) || mainWindow.analysisSession().isValid()
       || functionList->count() != 0 || !pseudocode->toPlainText().isEmpty()
       || backAction->isEnabled() || forwardAction->isEnabled()
       || !progress->isHidden()
       || !mainWindow.statusBar()->currentMessage().contains(QStringLiteral("Failed"))) {
        std::cerr << "Failed analysis did not leave a clean, informative GUI state\n";
        return 1;
    }

    return 0;
}
