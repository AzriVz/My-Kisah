#include "AnalysisSession.hpp"
#include "FunctionInfo.hpp"
#include "Instruction.hpp"
#include "MainWindow.hpp"

#include <QApplication>
#include <QLineEdit>
#include <QListWidget>
#include <QTableWidget>

#include <algorithm>
#include <iostream>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    if(argc < 2) {
        std::cerr << "Compiler-produced sample path is required\n";
        return 1;
    }

    decompiler::MainWindow mainWindow;
    if(!mainWindow.loadBinary(argv[1])) {
        std::cerr << "MainWindow could not analyze the sample binary\n";
        return 1;
    }

    auto* search = mainWindow.findChild<QLineEdit*>(QStringLiteral("functionSearch"));
    auto* functionList = mainWindow.findChild<QListWidget*>(QStringLiteral("functionList"));
    auto* assembly = mainWindow.findChild<QTableWidget*>(QStringLiteral("assemblyTable"));
    if(search == nullptr || functionList == nullptr || assembly == nullptr) {
        std::cerr << "Phase 3 widgets were not created\n";
        return 1;
    }

    if(functionList->count() < 2 || assembly->rowCount() == 0) {
        std::cerr << "Function list or assembly view was not populated\n";
        return 1;
    }

    const auto& session = mainWindow.analysisSession();
    const auto mainFunction = std::find_if(
        session.functions().begin(), session.functions().end(), [](const auto& function) {
            return function.name == "main";
        });
    if(mainFunction == session.functions().end()) {
        std::cerr << "main was not discovered\n";
        return 1;
    }

    search->setText(QStringLiteral("main"));
    QCoreApplication::processEvents();
    int visibleFunctions = 0;
    for(int row = 0; row < functionList->count(); ++row) {
        visibleFunctions += functionList->item(row)->isHidden() ? 0 : 1;
    }
    if(visibleFunctions != 1 || functionList->currentItem() == nullptr
       || !functionList->currentItem()->text().contains(QStringLiteral("main"))) {
        std::cerr << "Function name search did not filter in real time\n";
        return 1;
    }

    search->setText(QString::number(mainFunction->address, 16));
    QCoreApplication::processEvents();
    if(functionList->currentItem() == nullptr || functionList->currentItem()->isHidden()) {
        std::cerr << "Hexadecimal address search did not retain main\n";
        return 1;
    }

    search->clear();
    QCoreApplication::processEvents();
    const auto* mainInstructions = session.instructionsFor(mainFunction->address);
    if(mainInstructions == nullptr) {
        std::cerr << "main instruction cache is missing\n";
        return 1;
    }

    const auto directCall = std::find_if(
        mainInstructions->begin(), mainInstructions->end(), [&](const auto& instruction) {
            return instruction.kind == decompiler::InstructionKind::Call
                   && instruction.directTarget
                   && session.functionAt(*instruction.directTarget) != nullptr;
        });
    if(directCall == mainInstructions->end()) {
        std::cerr << "No navigable direct call exists in main\n";
        return 1;
    }

    const auto callRow = static_cast<int>(std::distance(mainInstructions->begin(), directCall));
    assembly->cellDoubleClicked(callRow, 0);
    QCoreApplication::processEvents();
    const auto* targetFunction = session.functionAt(*directCall->directTarget);
    if(targetFunction == nullptr || functionList->currentItem() == nullptr
       || !functionList->currentItem()->text().contains(
           QString::fromStdString(targetFunction->name))) {
        std::cerr << "Double-click call navigation did not select the callee\n";
        return 1;
    }

    return 0;
}

