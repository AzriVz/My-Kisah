#include "CallGraphPanel.hpp"

#include "CallGraph.hpp"
#include "CallGraphView.hpp"
#include "GraphOverviewWidget.hpp"
#include "IsolatedSubgraphTableModel.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QModelIndex>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableView>
#include <QToolBar>
#include <QVBoxLayout>

#include <optional>
#include <utility>

namespace decompiler {

CallGraphPanel::CallGraphPanel(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("callGraphPanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    auto* toolbar = new QToolBar(tr("Call Graph"), this);
    toolbar->setObjectName(QStringLiteral("callGraphToolBar"));
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setIconSize(QSize(16, 16));
    auto* zoomInAction = toolbar->addAction(tr("Zoom In"));
    zoomInAction->setObjectName(QStringLiteral("callGraphZoomInAction"));
    zoomInAction->setText(QStringLiteral("+"));
    zoomInAction->setToolTip(tr("Zoom In (Ctrl+Wheel Up)"));
    auto* zoomOutAction = toolbar->addAction(tr("Zoom Out"));
    zoomOutAction->setObjectName(QStringLiteral("callGraphZoomOutAction"));
    zoomOutAction->setText(QStringLiteral("−"));
    zoomOutAction->setToolTip(tr("Zoom Out (Ctrl+Wheel Down)"));
    auto* resetAction = toolbar->addAction(tr("Reset Zoom"));
    resetAction->setObjectName(QStringLiteral("callGraphResetZoomAction"));
    resetAction->setText(QStringLiteral("1:1"));
    toolbar->addSeparator();
    auto* fitAllAction = toolbar->addAction(tr("Fit All"));
    fitAllAction->setObjectName(QStringLiteral("callGraphFitAllAction"));
    auto* fitSelectionAction = toolbar->addAction(tr("Fit Selection"));
    fitSelectionAction->setObjectName(QStringLiteral("callGraphFitSelectionAction"));
    auto* fitComponentAction = toolbar->addAction(tr("Fit Component"));
    fitComponentAction->setObjectName(QStringLiteral("callGraphFitComponentAction"));
    toolbar->addSeparator();
    auto* refreshAction = toolbar->addAction(tr("Refresh Layout"));
    refreshAction->setObjectName(QStringLiteral("callGraphRefreshAction"));
    layout->addWidget(toolbar);

    auto* horizontalSplitter = new QSplitter(Qt::Horizontal, this);
    horizontalSplitter->setObjectName(QStringLiteral("callGraphHorizontalSplitter"));
    horizontalSplitter->setChildrenCollapsible(false);
    auto* leftSplitter = new QSplitter(Qt::Vertical, horizontalSplitter);
    leftSplitter->setObjectName(QStringLiteral("callGraphLeftSplitter"));
    leftSplitter->setChildrenCollapsible(false);

    auto* componentsGroup = new QGroupBox(tr("Isolated subgraphs"), leftSplitter);
    componentsGroup->setObjectName(QStringLiteral("callGraphComponentsGroup"));
    auto* componentsLayout = new QVBoxLayout(componentsGroup);
    componentsLayout->setContentsMargins(3, 5, 3, 3);
    componentTable_ = new QTableView(componentsGroup);
    componentTable_->setObjectName(QStringLiteral("callGraphComponentTable"));
    componentModel_ = new IsolatedSubgraphTableModel(componentTable_);
    componentTable_->setModel(componentModel_);
    componentTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    componentTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    componentTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    componentTable_->setAlternatingRowColors(true);
    componentTable_->setShowGrid(true);
    componentTable_->setWordWrap(false);
    componentTable_->verticalHeader()->setVisible(false);
    componentTable_->verticalHeader()->setDefaultSectionSize(21);
    componentTable_->horizontalHeader()->setStretchLastSection(true);
    componentTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    componentTable_->setColumnWidth(0, 62);
    componentTable_->setColumnWidth(1, 62);
    componentTable_->setColumnWidth(2, 48);
    componentTable_->setColumnWidth(3, 58);
    auto tableFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    tableFont.setPointSizeF(7.5);
    componentTable_->setFont(tableFont);
    componentsLayout->addWidget(componentTable_);

    auto* overviewGroup = new QGroupBox(tr("Graph overview"), leftSplitter);
    overviewGroup->setObjectName(QStringLiteral("callGraphOverviewGroup"));
    auto* overviewLayout = new QVBoxLayout(overviewGroup);
    overviewLayout->setContentsMargins(3, 5, 3, 3);
    overview_ = new GraphOverviewWidget(overviewGroup);
    overview_->setObjectName(QStringLiteral("callGraphOverview"));
    overviewLayout->addWidget(overview_);

    leftSplitter->addWidget(componentsGroup);
    leftSplitter->addWidget(overviewGroup);
    leftSplitter->setStretchFactor(0, 3);
    leftSplitter->setStretchFactor(1, 2);
    leftSplitter->setSizes({270, 180});

    graphView_ = new CallGraphView(horizontalSplitter);
    graphView_->setObjectName(QStringLiteral("callGraphView"));
    overview_->setGraphView(graphView_);
    horizontalSplitter->addWidget(leftSplitter);
    horizontalSplitter->addWidget(graphView_);
    horizontalSplitter->setStretchFactor(0, 1);
    horizontalSplitter->setStretchFactor(1, 4);
    horizontalSplitter->setSizes({280, 860});
    layout->addWidget(horizontalSplitter, 1);

    setStyleSheet(QStringLiteral(
        "#callGraphPanel { background: #F0F0F0; }"
        "#callGraphPanel QToolBar { background: #E5E5E5; border: 1px solid #A0A0A0; spacing: 2px; }"
        "#callGraphPanel QToolButton { padding: 2px 6px; border: 1px solid transparent; }"
        "#callGraphPanel QToolButton:hover { background: #F5F5F5; border-color: #888888; }"
        "#callGraphPanel QGroupBox { border: 1px solid #999999; margin-top: 15px; background: #F5F5F5; }"
        "#callGraphPanel QGroupBox::title { subcontrol-origin: margin; left: 5px; padding: 0 3px; }"
        "#callGraphComponentTable { background: #FFFFFF; alternate-background-color: #F0F0F0; gridline-color: #BDBDBD; }"
        "#callGraphComponentTable::item:selected { background: #D0D0D0; color: #202020; }"
        "#callGraphComponentTable QHeaderView::section { background: #E0E0E0; border: 1px solid #A8A8A8; padding: 2px; }"));

    connect(zoomInAction, &QAction::triggered, graphView_, &CallGraphView::zoomIn);
    connect(zoomOutAction, &QAction::triggered, graphView_, &CallGraphView::zoomOut);
    connect(resetAction, &QAction::triggered, graphView_, &CallGraphView::resetZoom);
    connect(fitAllAction, &QAction::triggered, graphView_, &CallGraphView::fitAll);
    connect(
        fitSelectionAction,
        &QAction::triggered,
        graphView_,
        &CallGraphView::fitSelection);
    connect(fitComponentAction, &QAction::triggered, this, &CallGraphPanel::fitCurrentComponent);
    connect(refreshAction, &QAction::triggered, this, &CallGraphPanel::refreshLayout);
    connect(componentTable_, &QTableView::clicked, this, &CallGraphPanel::focusComponent);
    connect(componentTable_, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        focusComponent(index);
        fitCurrentComponent();
    });
    graphView_->setSelectionChangedHandler(
        [this](std::optional<std::uint64_t> address) {
            if(address) {
                selectComponentForAddress(*address);
            } else {
                componentTable_->clearSelection();
            }
        });

    clearGraph();
}

void CallGraphPanel::setGraph(const CallGraph& graph) {
    graphView_->setGraph(graph);
    componentModel_->setComponents(graphView_->components());
    if(componentModel_->rowCount() > 0) {
        componentTable_->selectRow(0);
    }
    overview_->update();
}

void CallGraphPanel::clearGraph() {
    graphView_->clearGraph();
    componentModel_->clear();
    overview_->update();
}

void CallGraphPanel::setActiveFunction(std::uint64_t address) {
    graphView_->setActiveFunction(address);
    selectComponentForAddress(address);
}

void CallGraphPanel::setNodeActivationHandler(
    std::function<void(std::uint64_t)> handler) {
    graphView_->setNodeActivationHandler(std::move(handler));
}

void CallGraphPanel::setInstructionProvider(
    std::function<const std::vector<Instruction>*(std::uint64_t)> provider) {
    graphView_->setInstructionProvider(std::move(provider));
}

CallGraphView* CallGraphPanel::graphView() const noexcept {
    return graphView_;
}

QTableView* CallGraphPanel::componentTable() const noexcept {
    return componentTable_;
}

GraphOverviewWidget* CallGraphPanel::overview() const noexcept {
    return overview_;
}

void CallGraphPanel::fitCurrentComponent() {
    const auto current = componentTable_->currentIndex();
    if(current.isValid()) {
        static_cast<void>(graphView_->fitComponent(static_cast<std::size_t>(current.row())));
    }
}

void CallGraphPanel::refreshLayout() {
    graphView_->refreshLayout();
    componentModel_->setComponents(graphView_->components());
    if(const auto selected = graphView_->selectedFunction()) {
        selectComponentForAddress(*selected);
    }
    overview_->update();
}

void CallGraphPanel::focusComponent(const QModelIndex& index) {
    if(index.isValid()) {
        static_cast<void>(graphView_->fitComponent(static_cast<std::size_t>(index.row())));
    }
}

void CallGraphPanel::selectComponentForAddress(std::uint64_t address) {
    const auto componentIndex = graphView_->componentIndexForAddress(address);
    if(!componentIndex || *componentIndex >= static_cast<std::size_t>(componentModel_->rowCount())) {
        return;
    }
    const QSignalBlocker blocker(componentTable_->selectionModel());
    componentTable_->selectRow(static_cast<int>(*componentIndex));
    componentTable_->scrollTo(
        componentModel_->index(static_cast<int>(*componentIndex), 0),
        QAbstractItemView::PositionAtCenter);
}

} // namespace decompiler
