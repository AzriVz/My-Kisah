#pragma once

#include <QWidget>

#include <cstdint>
#include <functional>
#include <vector>

class QModelIndex;
class QTableView;

namespace decompiler {

class CallGraph;
class CallGraphView;
class GraphOverviewWidget;
class IsolatedSubgraphTableModel;
struct Instruction;

class CallGraphPanel final : public QWidget {
public:
    explicit CallGraphPanel(QWidget* parent = nullptr);

    void setGraph(const CallGraph& graph);
    void clearGraph();
    void setActiveFunction(std::uint64_t address);
    void setNodeActivationHandler(std::function<void(std::uint64_t)> handler);
    void setInstructionProvider(
        std::function<const std::vector<Instruction>*(std::uint64_t)> provider);

    [[nodiscard]] CallGraphView* graphView() const noexcept;
    [[nodiscard]] QTableView* componentTable() const noexcept;
    [[nodiscard]] GraphOverviewWidget* overview() const noexcept;

private:
    void fitCurrentComponent();
    void refreshLayout();
    void focusComponent(const QModelIndex& index);
    void selectComponentForAddress(std::uint64_t address);

    CallGraphView* graphView_ = nullptr;
    QTableView* componentTable_ = nullptr;
    GraphOverviewWidget* overview_ = nullptr;
    IsolatedSubgraphTableModel* componentModel_ = nullptr;
};

} // namespace decompiler
