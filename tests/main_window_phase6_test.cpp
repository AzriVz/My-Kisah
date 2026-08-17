#include "AnalysisSession.hpp"
#include "MainWindow.hpp"

#include <QApplication>
#include <QListWidget>
#include <QPlainTextEdit>

#include <iostream>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    if(argc < 2) {
        std::cerr << "Compiler-produced branching sample path is required\n";
        return 1;
    }

    decompiler::MainWindow mainWindow;
    if(!mainWindow.loadBinary(argv[1])) {
        std::cerr << "MainWindow could not analyze the branching sample\n";
        return 1;
    }

    auto* functionList = mainWindow.findChild<QListWidget*>(QStringLiteral("functionList"));
    auto* pseudocode =
        mainWindow.findChild<QPlainTextEdit*>(QStringLiteral("pseudocodeView"));
    if(functionList == nullptr || pseudocode == nullptr || !pseudocode->isReadOnly()) {
        std::cerr << "Read-only pseudocode view was not created\n";
        return 1;
    }

    for(int row = 0; row < functionList->count(); ++row) {
        if(!functionList->item(row)->text().contains(QStringLiteral("choose"))) {
            continue;
        }
        functionList->setCurrentRow(row);
        QCoreApplication::processEvents();
        if(!pseudocode->toPlainText().contains(QStringLiteral("if ("))
           || !pseudocode->toPlainText().contains(QStringLiteral("else"))) {
            std::cerr << "Selecting choose did not update the pseudocode panel\n";
            return 1;
        }
        return 0;
    }

    std::cerr << "choose was not present in the function list\n";
    return 1;
}
