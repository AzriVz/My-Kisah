#include "CallGraphPanel.hpp"
#include "CallGraphView.hpp"
#include "GraphOverviewWidget.hpp"
#include "MainWindow.hpp"
#include "PseudocodeGenerator.hpp"
#include "PseudocodeView.hpp"

#include "AnalysisSession.hpp"

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QSplitter>
#include <QTableView>
#include <QTableWidget>
#include <QTextCursor>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <system_error>

static int failures = 0;

static void expect(bool condition, std::string_view message) {
    if(!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

static const decompiler::Instruction* findInternalCall(
    const decompiler::AnalysisSession& session,
    std::uint64_t& caller,
    std::uint64_t& callee) {
    for(const auto& function : session.functions()) {
        const auto* instructions = session.instructionsFor(function.address);
        if(instructions == nullptr) {
            continue;
        }
        for(const auto& instruction : *instructions) {
            if(instruction.kind == decompiler::InstructionKind::Call
               && instruction.directTarget
               && session.functionAt(*instruction.directTarget) != nullptr) {
                caller = function.address;
                callee = *instruction.directTarget;
                return &instruction;
            }
        }
    }
    return nullptr;
}

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    if(argc != 2) {
        std::cerr << "expected optimized PIE sample path\n";
        return 2;
    }

    decompiler::MainWindow window;
    expect(window.loadBinary(argv[1]), "phase 8 PIE should load in the GUI");
    auto* graphPanel = window.findChild<decompiler::CallGraphPanel*>(
        QStringLiteral("callGraphPanel"));
    auto* graphView = window.findChild<decompiler::CallGraphView*>(QStringLiteral("callGraphView"));
    auto* componentTable = window.findChild<QTableView*>(
        QStringLiteral("callGraphComponentTable"));
    auto* overview = window.findChild<decompiler::GraphOverviewWidget*>(
        QStringLiteral("callGraphOverview"));
    auto* pseudocode = window.findChild<decompiler::PseudocodeView*>(
        QStringLiteral("pseudocodeView"));
    expect(graphPanel != nullptr, "IDA-style call graph panel should exist");
    expect(graphView != nullptr, "call graph view should exist");
    expect(
        componentTable != nullptr && componentTable->model()->rowCount() > 0,
        "component table should be populated from the analyzed binary");
    expect(overview != nullptr && overview->hasGraphData(), "graph minimap should be populated");
    expect(pseudocode != nullptr, "pseudocode view should exist");
    expect(
        window.findChild<QSplitter*>(QStringLiteral("callGraphHorizontalSplitter")) != nullptr,
        "call graph sidebar should be resizable");
    expect(
        window.findChild<QAction*>(QStringLiteral("callGraphFitAllAction")) != nullptr,
        "call graph toolbar should provide Fit All");
    if(graphView != nullptr) {
        expect(
            graphView->nodeCount() == window.analysisSession().callGraph().nodes().size(),
            "GUI graph should refresh from analysis");
        expect(
            graphView->edgeCount() == window.analysisSession().callGraph().edges().size(),
            "GUI graph edge count should match analysis");
        const auto beforeZoom = graphView->zoomFactor();
        graphView->zoomIn();
        expect(graphView->zoomFactor() > beforeZoom, "GUI call graph should zoom in");
        for(const auto& node : window.analysisSession().callGraph().nodes()) {
            if(node.isExternal) {
                expect(
                    graphView->displayedAssemblyLineCount(node.address) == 0,
                    "external nodes should not claim local assembly instructions");
                continue;
            }
            const auto* instructions = window.analysisSession().instructionsFor(node.address);
            expect(
                instructions != nullptr && !instructions->empty()
                    && graphView->displayedAssemblyLineCount(node.address) > 0,
                "every internal graph node should display its analyzed assembly");
        }
    }

    auto* classLabel = window.findChild<QLabel*>(QStringLiteral("elfClassValue"));
    expect(
        classLabel != nullptr && classLabel->text().contains(QStringLiteral("PIE")),
        "binary information should identify PIE");

    std::uint64_t caller = 0;
    std::uint64_t callee = 0;
    const auto* call = findInternalCall(window.analysisSession(), caller, callee);
    expect(call != nullptr, "GUI sample should have an internal call to patch");
    if(call != nullptr && graphView != nullptr) {
        expect(graphView->activateNode(callee), "internal graph node should navigate");
        expect(graphView->activeFunction() == callee, "clicked graph node should become active");
        const auto* calleeFunction = window.analysisSession().functionAt(callee);
        const auto calleeIdentifier = calleeFunction == nullptr
                                          ? std::string {}
                                          : decompiler::PseudocodeGenerator::identifierForFunction(
                                                calleeFunction->name,
                                                calleeFunction->address);
        expect(
            pseudocode != nullptr && !calleeIdentifier.empty()
                && pseudocode->textCursor().hasSelection()
                && pseudocode->textCursor().selectedText().toStdString().find(calleeIdentifier)
                       != std::string::npos,
            "Call Graph navigation should select the reconstructed function declaration");

        const auto address = call->address;
        const auto size = call->bytes.size();
        const auto uniqueValue = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto outputPath = std::filesystem::temp_directory_path()
                                / ("phase8-gui-patched-" + std::to_string(uniqueValue));
        const std::vector<std::uint8_t> nops(size, 0x90);
        expect(
            window.patchInstruction(address, nops, outputPath),
            "GUI patch should save and automatically re-analyze");
        expect(
            window.analysisSession().elfLoader().metadata().filePath == outputPath,
            "GUI should switch to the patched output");

        bool staleEdge = false;
        for(const auto& edge : window.analysisSession().callGraph().edges()) {
            staleEdge = staleEdge
                        || (edge.callerAddress == caller && edge.calleeAddress == callee);
        }
        expect(!staleEdge, "GUI call graph should refresh after patching a call");
        std::error_code error;
        std::filesystem::remove(outputPath, error);
    }

    return failures == 0 ? 0 : 1;
}
