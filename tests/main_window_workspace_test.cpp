#include "AnalysisSession.hpp"
#include "AssemblyGraphTable.hpp"
#include "CallGraphView.hpp"
#include "Instruction.hpp"
#include "MainWindow.hpp"

#include <QApplication>
#include <QAction>
#include <QListWidget>
#include <QPixmap>
#include <QScrollBar>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QToolBar>

#include <cstdint>
#include <iostream>
#include <string_view>
#include <unordered_set>

static int failures = 0;

static void expect(bool condition, std::string_view message) {
    if(!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    if(argc != 2) {
        std::cerr << "expected branching sample path\n";
        return 2;
    }

    decompiler::MainWindow window;
    expect(window.loadBinary(argv[1]), "branching sample should load");

    auto* splitter = window.findChild<QSplitter*>(QStringLiteral("analysisSplitter"));
    auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("analysisTabs"));
    auto* functions = window.findChild<QListWidget*>(QStringLiteral("functionList"));
    auto* symbolTree = window.findChild<QTreeWidget*>(QStringLiteral("symbolTree"));
    auto* pseudocodePanel = window.findChild<QWidget*>(QStringLiteral("pseudocodePanel"));
    auto* assemblyPanel = window.findChild<QWidget*>(QStringLiteral("assemblyPanel"));
    auto* assemblyBase = window.findChild<QTableWidget*>(QStringLiteral("assemblyTable"));
    auto* assembly = dynamic_cast<decompiler::AssemblyGraphTable*>(assemblyBase);
    auto* assemblyScrollBar = window.findChild<QScrollBar*>(
        QStringLiteral("assemblyHorizontalScrollBar"));
    auto* navigationToolBar = window.findChild<QToolBar*>(QStringLiteral("navigationToolBar"));
    const auto toolbarActions = {
        window.findChild<QAction*>(QStringLiteral("openBinaryAction")),
        window.findChild<QAction*>(QStringLiteral("backAction")),
        window.findChild<QAction*>(QStringLiteral("forwardAction")),
        window.findChild<QAction*>(QStringLiteral("patchInstructionAction")),
    };
    if(qEnvironmentVariableIsSet("MY_KISAH_WORKSPACE_SCREENSHOT")) {
        decompiler::CallGraphView* previewGraph = nullptr;
        if(tabs != nullptr
           && qEnvironmentVariable("MY_KISAH_WORKSPACE_VIEW")
                  == QStringLiteral("call-graph")) {
            tabs->setCurrentIndex(1);
            previewGraph = window.findChild<decompiler::CallGraphView*>(
                QStringLiteral("callGraphView"));
        }
        window.show();
        QApplication::processEvents();
        if(previewGraph != nullptr) {
            previewGraph->fitSelection();
            QApplication::processEvents();
        }
        static_cast<void>(window.grab().save(
            qEnvironmentVariable("MY_KISAH_WORKSPACE_SCREENSHOT")));
    }

    expect(splitter != nullptr && splitter->count() == 3, "workspace should have three columns");
    if(splitter != nullptr && splitter->count() == 3) {
        expect(
            symbolTree != nullptr && splitter->widget(0)->isAncestorOf(symbolTree),
            "Symbol Tree should occupy the left column");
        expect(splitter->widget(1) == tabs, "analysis tabs should occupy the center column");
        expect(
            splitter->widget(2) == pseudocodePanel,
            "Pseudocode should occupy the right column");
    }
    expect(
        tabs != nullptr && tabs->count() == 2
            && tabs->tabText(0) == QStringLiteral("Assembly")
            && tabs->tabText(1) == QStringLiteral("Call Graph")
            && tabs->widget(0) == assemblyPanel,
        "center tabs should contain Assembly followed by Call Graph");
    expect(assembly != nullptr, "assembly should use the flow-graph table");
    expect(
        assemblyScrollBar != nullptr,
        "assembly should expose a Call Graph-style horizontal scrollbar");
    expect(
        navigationToolBar != nullptr
            && navigationToolBar->toolButtonStyle() == Qt::ToolButtonIconOnly
            && navigationToolBar->iconSize() == QSize(22, 22),
        "main toolbar should use consistently sized icon-only buttons");
    for(const auto* action : toolbarActions) {
        expect(
            action != nullptr && !action->icon().isNull()
                && !action->toolTip().isEmpty(),
            "every primary toolbar action should expose its own icon and tooltip");
    }

    const auto& session = window.analysisSession();
    const auto categoryNamed = [symbolTree](const QString& name) {
        if(symbolTree == nullptr) {
            return static_cast<QTreeWidgetItem*>(nullptr);
        }
        for(int index = 0; index < symbolTree->topLevelItemCount(); ++index) {
            auto* category = symbolTree->topLevelItem(index);
            if(category->text(0) == name) {
                return category;
            }
        }
        return static_cast<QTreeWidgetItem*>(nullptr);
    };
    auto* importsCategory = categoryNamed(QStringLiteral("Imports"));
    auto* exportsCategory = categoryNamed(QStringLiteral("Exports"));
    auto* functionsCategory = categoryNamed(QStringLiteral("Functions"));
    auto* labelsCategory = categoryNamed(QStringLiteral("Labels"));
    auto* dataCategory = categoryNamed(QStringLiteral("Data"));
    auto* sectionsCategory = categoryNamed(QStringLiteral("Sections"));
    expect(
        importsCategory != nullptr && exportsCategory != nullptr
            && functionsCategory != nullptr && labelsCategory != nullptr
            && dataCategory != nullptr && sectionsCategory != nullptr
            && categoryNamed(QStringLiteral("Classes")) != nullptr
            && categoryNamed(QStringLiteral("Namespaces")) != nullptr,
        "Symbol Tree should expose Ghidra-style symbol categories");
    expect(
        functionsCategory != nullptr
            && functionsCategory->childCount()
                   == static_cast<int>(session.functions().size()),
        "Functions category should contain every discovered function");
    if(importsCategory != nullptr && importsCategory->childCount() > 0
       && exportsCategory != nullptr && exportsCategory->childCount() > 0
       && functionsCategory != nullptr && functionsCategory->childCount() > 0) {
        const auto importIcon = importsCategory->child(0)->icon(0);
        const auto exportIcon = exportsCategory->child(0)->icon(0);
        const auto functionIcon = functionsCategory->child(0)->icon(0);
        expect(
            !importIcon.isNull() && !exportIcon.isNull() && !functionIcon.isNull()
                && importIcon.cacheKey() != exportIcon.cacheKey()
                && importIcon.cacheKey() != functionIcon.cacheKey()
                && exportIcon.cacheKey() != functionIcon.cacheKey(),
            "imports, exports, and functions should use distinct symbol icons");
        expect(
            importsCategory->icon(0).cacheKey() != exportsCategory->icon(0).cacheKey()
                && importsCategory->icon(0).cacheKey()
                       != functionsCategory->icon(0).cacheKey(),
            "symbol category folders should carry distinct badges");
    }
    expect(
        sectionsCategory != nullptr && sectionsCategory->childCount() > 0,
        "Sections category should be populated from ELF metadata");
    expect(
        (importsCategory != nullptr && importsCategory->childCount() > 0)
            || (exportsCategory != nullptr && exportsCategory->childCount() > 0)
            || (labelsCategory != nullptr && labelsCategory->childCount() > 0)
            || (dataCategory != nullptr && dataCategory->childCount() > 0),
        "Symbol Tree should expose symbols beyond Functions");
    if(functionsCategory != nullptr && functionsCategory->childCount() > 0
       && functions != nullptr) {
        auto* functionItem = functionsCategory->child(0);
        expect(
            functionItem->text(0).contains(QStringLiteral("bytes"))
                && functionItem->text(0).contains(QStringLiteral("0x")),
            "function entries should show address and size");
        symbolTree->itemClicked(functionItem, 0);
        QApplication::processEvents();
        expect(
            functions->currentItem() != nullptr
                && functions->currentItem()->data(Qt::UserRole + 1)
                       == functionItem->data(0, Qt::UserRole + 1),
            "clicking a Symbol Tree function should update function navigation");

        if(functionsCategory->childCount() > 1) {
            auto* keyboardItem = functionsCategory->child(1);
            symbolTree->itemActivated(keyboardItem, 0);
            QApplication::processEvents();
            expect(
                functions->currentItem() != nullptr
                    && functions->currentItem()->data(Qt::UserRole + 1)
                           == keyboardItem->data(0, Qt::UserRole + 1),
                "activating a Symbol Tree function with Enter should navigate");
        }
    }

    std::size_t totalInstructionCount = 0;
    std::unordered_set<std::uint64_t> instructionAddresses;
    for(const auto& function : session.functions()) {
        if(const auto* instructions = session.instructionsFor(function.address)) {
            totalInstructionCount += instructions->size();
            for(const auto& instruction : *instructions) {
                instructionAddresses.insert(instruction.address);
            }
        }
    }
    if(assembly != nullptr) {
        window.show();
        QApplication::processEvents();
        expect(
            assembly->rowCount() == static_cast<int>(totalInstructionCount),
            "assembly listing should contain instructions from every function");
        if(assemblyScrollBar != nullptr) {
            expect(
                assembly->horizontalScrollBar() == assemblyScrollBar
                    && assembly->horizontalScrollBarPolicy() == Qt::ScrollBarAsNeeded
                    && assembly->horizontalScrollMode()
                           == QAbstractItemView::ScrollPerPixel,
                "assembly should use its native per-pixel scrollbar like Call Graph");
            expect(
                assemblyScrollBar->isVisible() && assemblyScrollBar->maximum() > 0,
                "assembly listing should provide horizontal content to slide");
            const auto initialColumnPosition = assembly->columnViewportPosition(0);
            assemblyScrollBar->setValue(assemblyScrollBar->maximum());
            QApplication::processEvents();
            expect(
                assembly->columnViewportPosition(0) != initialColumnPosition,
                "moving the assembly scrollbar should slide the listing");
            assemblyScrollBar->setValue(0);
        }
        const auto* firstAddress = assembly->item(0, 0);
        expect(
            firstAddress != nullptr
                && firstAddress->text().contains(
                    QString::number(session.elfLoader().metadata().entryPoint, 16),
                    Qt::CaseInsensitive),
            "assembly listing should begin with the entry-point function");

        std::size_t expectedFlowEdges = 0;
        for(const auto& function : session.functions()) {
            const auto* instructions = session.instructionsFor(function.address);
            if(instructions == nullptr) {
                continue;
            }
            for(const auto& instruction : *instructions) {
                const auto isDirectBranch = instruction.directTarget
                                            && (instruction.kind
                                                    == decompiler::InstructionKind::ConditionalJump
                                                || instruction.kind
                                                       == decompiler::InstructionKind::UnconditionalJump);
                if(isDirectBranch && instructionAddresses.contains(*instruction.directTarget)) {
                    ++expectedFlowEdges;
                }
            }
        }
        expect(expectedFlowEdges > 0, "branching sample should expose assembly flow edges");
        expect(
            assembly->flowEdgeCount() == expectedFlowEdges,
            "assembly gutter should represent every resolved direct branch");
    }

    return failures == 0 ? 0 : 1;
}
