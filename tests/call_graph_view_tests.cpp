#include "CallGraph.hpp"
#include "CallGraphPanel.hpp"
#include "CallGraphView.hpp"
#include "GraphOverviewWidget.hpp"

#include <QAction>
#include <QApplication>
#include <QLineF>
#include <QMouseEvent>
#include <QSplitter>
#include <QTableView>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

static int failures = 0;

static void expect(bool condition, std::string_view message) {
    if(!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

static decompiler::Instruction directCall(std::uint64_t address, std::uint64_t target) {
    decompiler::Instruction instruction;
    instruction.address = address;
    instruction.bytes = {0xE8, 0, 0, 0, 0};
    instruction.mnemonic = "call";
    instruction.kind = decompiler::InstructionKind::Call;
    instruction.directTarget = target;
    return instruction;
}

static decompiler::Instruction assemblyInstruction(
    std::uint64_t address,
    std::string mnemonic,
    std::string operands = {}) {
    decompiler::Instruction instruction;
    instruction.address = address;
    instruction.mnemonic = std::move(mnemonic);
    instruction.operandText = std::move(operands);
    return instruction;
}

static void addAssemblyPreviews(
    std::unordered_map<std::uint64_t, std::vector<decompiler::Instruction>>& cache,
    const decompiler::CallGraph& graph) {
    for(const auto& node : graph.nodes()) {
        if(node.isExternal || cache.contains(node.address)) {
            continue;
        }
        cache.emplace(
            node.address,
            std::vector<decompiler::Instruction> {
                assemblyInstruction(node.address, "push", "rbp"),
                assemblyInstruction(node.address + 1, "mov", "rbp, rsp"),
                assemblyInstruction(node.address + 4, "sub", "rsp, 0x20"),
                assemblyInstruction(node.address + 8, "xor", "eax, eax"),
                assemblyInstruction(node.address + 10, "nop"),
                assemblyInstruction(node.address + 11, "leave"),
                assemblyInstruction(node.address + 12, "ret"),
            });
    }
}

static bool buildGraph(
    decompiler::CallGraph& graph,
    const std::vector<decompiler::FunctionInfo>& functions,
    const std::vector<std::pair<std::uint64_t, std::uint64_t>>& edges) {
    std::unordered_map<std::uint64_t, std::vector<decompiler::Instruction>> instructions;
    instructions.reserve(functions.size());
    for(const auto& function : functions) {
        instructions.emplace(function.address, std::vector<decompiler::Instruction> {});
    }
    std::uint64_t callSite = 0x10;
    for(const auto& [caller, callee] : edges) {
        instructions[caller].push_back(directCall(caller + callSite, callee));
        callSite += 5;
    }
    return graph.build(functions, instructions);
}

static bool pathsAreOrthogonal(const std::vector<QPainterPath>& paths) {
    for(const auto& path : paths) {
        for(int index = 1; index < path.elementCount(); ++index) {
            const auto previous = path.elementAt(index - 1);
            const auto current = path.elementAt(index);
            const auto horizontal = std::abs(previous.y - current.y) < 0.001;
            const auto vertical = std::abs(previous.x - current.x) < 0.001;
            if(!horizontal && !vertical) {
                return false;
            }
        }
    }
    return true;
}

static bool nodeBoundsDoNotOverlap(const std::vector<QRectF>& bounds) {
    for(std::size_t left = 0; left < bounds.size(); ++left) {
        for(std::size_t right = left + 1; right < bounds.size(); ++right) {
            if(bounds[left].intersects(bounds[right])) {
                return false;
            }
        }
    }
    return true;
}

static decompiler::CallGraph makeComplexGraph() {
    const std::vector<decompiler::FunctionInfo> functions = {
        {.name = "main", .address = 0x1000, .size = 0x54},
        {.name = "left_branch", .address = 0x1100, .size = 0x24},
        {.name = "right_branch", .address = 0x1200, .size = 0x28},
        {.name = "shared_leaf", .address = 0x1300, .size = 0x18},
        {.name = "isolated", .address = 0x1400, .size = 0x10},
        {.name = "recursive", .address = 0x1500, .size = 0x30},
    };
    const std::vector<std::pair<std::uint64_t, std::uint64_t>> edges = {
        {0x1000, 0x1100},
        {0x1000, 0x1200},
        {0x1100, 0x1300},
        {0x1200, 0x1300},
        {0x1200, 0x9000},
        {0x1500, 0x1500},
    };
    decompiler::CallGraph graph;
    static_cast<void>(buildGraph(graph, functions, edges));
    return graph;
}

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);

    decompiler::CallGraph emptyGraph;
    expect(buildGraph(emptyGraph, {}, {}), "empty graph fixture should build");
    std::unordered_map<std::uint64_t, std::vector<decompiler::Instruction>> assemblyCache;
    decompiler::CallGraphView view;
    view.setInstructionProvider([&assemblyCache](std::uint64_t address) {
        const auto instructions = assemblyCache.find(address);
        return instructions == assemblyCache.end() ? nullptr : &instructions->second;
    });
    view.resize(760, 480);
    view.show();
    view.setGraph(emptyGraph);
    expect(view.nodeCount() == 0, "empty graph should not create node items");
    expect(view.components().empty(), "empty graph should not create components");

    decompiler::CallGraph singleGraph;
    expect(
        buildGraph(
            singleGraph,
            {{.name = "single", .address = 0x2000, .size = 0x10}},
            {}),
        "single-node graph should build");
    addAssemblyPreviews(assemblyCache, singleGraph);
    view.setGraph(singleGraph);
    expect(view.nodeCount() == 1, "single node should render");
    expect(view.edgeCount() == 0, "single node should not invent edges");
    expect(view.components().size() == 1, "single node should form one component");
    expect(
        view.displayedAssemblyLineCount(0x2000) == assemblyCache.at(0x2000).size(),
        "node should display every instruction supplied by its provider");
    const auto singleNodeRect = view.nodeSceneRect(0x2000);
    expect(
        singleNodeRect.has_value() && singleNodeRect->height() > 158.0,
        "node height should grow when assembly exceeds the former six-line preview");

    auto graph = makeComplexGraph();
    addAssemblyPreviews(assemblyCache, graph);
    view.setGraph(graph);
    QApplication::processEvents();
    expect(view.nodeCount() == 7, "internal and external nodes should render");
    expect(view.edgeCount() == 6, "branching and recursive edges should render");
    expect(view.components().size() == 3, "disconnected graph should form three components");
    for(const auto& node : graph.nodes()) {
        const auto expectedInstructionCount = node.isExternal
                                                  ? 0
                                                  : assemblyCache.at(node.address).size();
        expect(
            view.displayedAssemblyLineCount(node.address) == expectedInstructionCount,
            "every internal node should display all assembly while external nodes remain explicit");
    }
    expect(pathsAreOrthogonal(view.edgeScenePaths()), "every edge segment should be orthogonal");
    expect(nodeBoundsDoNotOverlap(view.nodeSceneBounds()), "hierarchical nodes should not overlap");

    std::size_t totalNodes = 0;
    for(const auto& component : view.components()) {
        totalNodes += component.nodeAddresses.size();
        expect(!component.sceneBounds.isEmpty(), "component should expose focus bounds");
    }
    expect(totalNodes == view.nodeCount(), "component counts should cover every node exactly once");
    expect(view.fitComponent(0), "known component should fit");
    expect(!view.fitComponent(99), "unknown component should be rejected");

    std::uint64_t activated = 0;
    view.setNodeActivationHandler([&activated](std::uint64_t address) { activated = address; });
    const auto mainRect = view.nodeSceneRect(0x1000);
    expect(mainRect.has_value(), "main node should expose a scene rectangle");
    if(mainRect) {
        const auto local = view.mapFromScene(mainRect->center());
        const auto global = view.viewport()->mapToGlobal(local);
        QMouseEvent press(
            QEvent::MouseButtonPress,
            QPointF(local),
            QPointF(global),
            Qt::LeftButton,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &press);
        QMouseEvent release(
            QEvent::MouseButtonRelease,
            QPointF(local),
            QPointF(global),
            Qt::LeftButton,
            Qt::NoButton,
            Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &release);
        expect(activated == 0x1000, "single click should activate existing navigation");

        activated = 0;
        QMouseEvent doubleClick(
            QEvent::MouseButtonDblClick,
            QPointF(local),
            QPointF(global),
            Qt::LeftButton,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &doubleClick);
        expect(activated == 0x1000, "double click should activate existing navigation");
    }

    view.setActiveFunction(0x1300);
    expect(view.activeFunction() == 0x1300, "active function should be tracked");
    expect(view.selectedFunction() == 0x1300, "active function should synchronize selection");
    const auto originalZoom = view.zoomFactor();
    view.zoomIn();
    expect(view.zoomFactor() > originalZoom, "zoom in should increase scale");
    view.zoomOut();
    expect(
        std::abs(view.zoomFactor() - originalZoom) < 0.001,
        "zoom out should reverse one zoom-in step");
    view.resetZoom();
    expect(std::abs(view.zoomFactor() - 1.0) < 0.001, "reset should restore unit scale");
    view.refreshLayout();
    expect(view.selectedFunction() == 0x1300, "refresh should preserve selected node");

    decompiler::CallGraphPanel panel;
    panel.setInstructionProvider([&assemblyCache](std::uint64_t address) {
        const auto instructions = assemblyCache.find(address);
        return instructions == assemblyCache.end() ? nullptr : &instructions->second;
    });
    panel.resize(980, 620);
    panel.show();
    panel.setGraph(graph);
    QApplication::processEvents();
    expect(panel.graphView()->nodeCount() == 7, "panel should use backend graph nodes");
    expect(panel.componentTable()->model()->rowCount() == 3, "component table should list subgraphs");
    expect(panel.overview()->hasGraphData(), "minimap should receive main graph data");
    expect(
        !panel.overview()->overviewViewportRect().isEmpty(),
        "minimap should draw the main viewport rectangle");
    expect(
        panel.findChild<QSplitter*>(QStringLiteral("callGraphHorizontalSplitter")) != nullptr,
        "panel should provide a resizable horizontal splitter");
    expect(
        panel.findChild<QSplitter*>(QStringLiteral("callGraphLeftSplitter")) != nullptr,
        "sidebar should provide a resizable vertical splitter");
    expect(
        panel.findChild<QAction*>(QStringLiteral("callGraphFitAllAction")) != nullptr,
        "call graph toolbar should expose Fit All");
    expect(
        panel.findChild<QAction*>(QStringLiteral("callGraphFitSelectionAction")) != nullptr,
        "call graph toolbar should expose Fit Selection");
    expect(
        panel.findChild<QAction*>(QStringLiteral("callGraphRefreshAction")) != nullptr,
        "call graph toolbar should expose Refresh Layout");

    auto* refreshAction = panel.findChild<QAction*>(QStringLiteral("callGraphRefreshAction"));
    if(refreshAction != nullptr) {
        panel.setActiveFunction(0x1100);
        refreshAction->trigger();
        expect(
            panel.graphView()->selectedFunction() == 0x1100,
            "toolbar refresh should preserve graph selection");
    }

    panel.graphView()->resetZoom();
    panel.graphView()->zoomIn();
    panel.graphView()->zoomIn();
    const auto targetPoint = panel.graphView()->scene()->sceneRect().bottomRight();
    const auto beforeCenter = panel.graphView()->mapToScene(
        panel.graphView()->viewport()->rect().center());
    panel.overview()->navigateToScenePoint(targetPoint);
    const auto afterCenter = panel.graphView()->mapToScene(
        panel.graphView()->viewport()->rect().center());
    expect(
        QLineF(afterCenter, targetPoint).length() <= QLineF(beforeCenter, targetPoint).length(),
        "minimap navigation should move the main view toward the requested point");

    std::vector<decompiler::FunctionInfo> largeFunctions;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> largeEdges;
    constexpr std::size_t largeNodeCount = 160;
    largeFunctions.reserve(largeNodeCount);
    largeEdges.reserve(largeNodeCount - 1);
    for(std::size_t index = 0; index < largeNodeCount; ++index) {
        const auto address = 0x10000 + static_cast<std::uint64_t>(index) * 0x20;
        largeFunctions.push_back(decompiler::FunctionInfo {
            .name = "function_" + std::to_string(index),
            .address = address,
            .size = 0x20,
        });
        if(index > 0) {
            largeEdges.emplace_back(address - 0x20, address);
        }
    }
    decompiler::CallGraph largeGraph;
    expect(buildGraph(largeGraph, largeFunctions, largeEdges), "large graph should build");
    addAssemblyPreviews(assemblyCache, largeGraph);
    panel.setGraph(largeGraph);
    expect(panel.graphView()->nodeCount() == largeNodeCount, "large graph should remain navigable");
    expect(panel.graphView()->components().size() == 1, "linear large graph should stay connected");

    panel.setGraph(singleGraph);
    expect(panel.graphView()->nodeCount() == 1, "opening a second graph should replace old items");
    expect(panel.componentTable()->model()->rowCount() == 1, "component table should refresh");
    panel.clearGraph();
    expect(
        panel.graphView()->nodeCount() == 0 && !panel.overview()->hasGraphData(),
        "clearing the panel should also clear its minimap");

    return failures == 0 ? 0 : 1;
}
