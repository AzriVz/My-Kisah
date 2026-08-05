#include "MainWindow.hpp"

#include "AnalysisSession.hpp"
#include "AssemblyGraphTable.hpp"
#include "BinaryPatcher.hpp"
#include "CallGraphPanel.hpp"
#include "CallGraphView.hpp"
#include "ElfLoader.hpp"
#include "FunctionInfo.hpp"
#include "Instruction.hpp"
#include "PseudocodeGenerator.hpp"
#include "PseudocodeHighlighter.hpp"
#include "PseudocodeView.hpp"

#include <elf.h>

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QEventLoop>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <unordered_map>

static constexpr int addressRole = Qt::UserRole + 1;
static constexpr int directTargetRole = Qt::UserRole + 2;
static constexpr int instructionKindRole = Qt::UserRole + 3;
static constexpr int functionNameRole = Qt::UserRole + 4;
static constexpr int assemblyFunctionAddressRole = Qt::UserRole + 5;
static constexpr int symbolItemKindRole = Qt::UserRole + 6;
static constexpr int symbolCategoryRole = Qt::UserRole + 7;

enum class SymbolTreeItemKind {
    Category,
    Function,
    CodeSymbol,
    DataSymbol,
    Import,
    Section,
};

static QLabel* createValueLabel(QWidget* parent, const char* objectName) {
    auto* label = new QLabel(parent);
    label->setObjectName(QString::fromLatin1(objectName));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    label->setWordWrap(true);
    return label;
}

static QString hexadecimal(std::uint64_t value) {
    return QStringLiteral("0x") + QString::number(value, 16).toUpper();
}

static QString byteString(const std::vector<std::uint8_t>& bytes) {
    QStringList encodedBytes;
    encodedBytes.reserve(static_cast<qsizetype>(bytes.size()));
    for(const auto byte : bytes) {
        encodedBytes.push_back(
            QStringLiteral("%1").arg(static_cast<unsigned int>(byte), 2, 16, QLatin1Char('0')));
    }
    return encodedBytes.join(QLatin1Char(' ')).toUpper();
}

static QString sourceDescription(decompiler::FunctionSource source) {
    using decompiler::FunctionSource;
    switch(source) {
    case FunctionSource::SymbolTable:
        return QStringLiteral("Static symbol table");
    case FunctionSource::DynamicSymbolTable:
        return QStringLiteral("Dynamic symbol table");
    case FunctionSource::EntryPoint:
        return QStringLiteral("ELF entry point");
    case FunctionSource::DirectCallTarget:
        return QStringLiteral("Direct call target");
    case FunctionSource::Heuristic:
        return QStringLiteral("Heuristic");
    }
    return QStringLiteral("Unknown");
}

static QColor instructionColor(decompiler::InstructionKind kind) {
    using decompiler::InstructionKind;
    switch(kind) {
    case InstructionKind::Call:
        return QColor(QStringLiteral("#2563EB"));
    case InstructionKind::ConditionalJump:
    case InstructionKind::UnconditionalJump:
    case InstructionKind::IndirectJump:
        return QColor(QStringLiteral("#D97706"));
    case InstructionKind::Return:
        return QColor(QStringLiteral("#7C3AED"));
    case InstructionKind::Invalid:
        return QColor(QStringLiteral("#DC2626"));
    case InstructionKind::Normal:
        return {};
    }
    return {};
}

namespace decompiler {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , analysisSession_(std::make_unique<AnalysisSession>()) {
    setWindowTitle(tr("Decompiler"));
    resize(1560, 860);

    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    auto* openAction = fileMenu->addAction(tr("&Open Binary..."));
    openAction->setObjectName(QStringLiteral("openBinaryAction"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::chooseBinary);

    fileMenu->addSeparator();
    auto* quitAction = fileMenu->addAction(tr("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    auto* navigateMenu = menuBar()->addMenu(tr("&Navigate"));
    backAction_ = navigateMenu->addAction(tr("&Back"));
    backAction_->setObjectName(QStringLiteral("backAction"));
    backAction_->setShortcut(QKeySequence(QStringLiteral("Alt+Left")));
    connect(backAction_, &QAction::triggered, this, &MainWindow::navigateBack);

    forwardAction_ = navigateMenu->addAction(tr("&Forward"));
    forwardAction_->setObjectName(QStringLiteral("forwardAction"));
    forwardAction_->setShortcut(QKeySequence(QStringLiteral("Alt+Right")));
    connect(forwardAction_, &QAction::triggered, this, &MainWindow::navigateForward);

    auto* findPseudocodeAction = navigateMenu->addAction(tr("Find in &Pseudocode"));
    findPseudocodeAction->setObjectName(QStringLiteral("findPseudocodeAction"));
    findPseudocodeAction->setShortcut(QKeySequence::Find);
    connect(findPseudocodeAction, &QAction::triggered, this, [this] {
        pseudocodeSearch_->setFocus();
        pseudocodeSearch_->selectAll();
    });

    auto* patchMenu = menuBar()->addMenu(tr("&Patch"));
    patchInstructionAction_ = patchMenu->addAction(tr("Patch Selected &Instruction..."));
    patchInstructionAction_->setObjectName(QStringLiteral("patchInstructionAction"));
    patchInstructionAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+P")));
    patchInstructionAction_->setEnabled(false);
    connect(
        patchInstructionAction_,
        &QAction::triggered,
        this,
        &MainWindow::patchSelectedInstruction);

    auto* navigationToolBar = addToolBar(tr("Navigation"));
    navigationToolBar->setObjectName(QStringLiteral("navigationToolBar"));
    navigationToolBar->setMovable(false);
    navigationToolBar->addAction(openAction);
    navigationToolBar->addSeparator();
    navigationToolBar->addAction(backAction_);
    navigationToolBar->addAction(forwardAction_);
    navigationToolBar->addSeparator();
    navigationToolBar->addAction(patchInstructionAction_);

    auto* centralWidget = new QWidget(this);
    auto* pageLayout = new QVBoxLayout(centralWidget);

    auto* informationGroup = new QGroupBox(tr("Binary Information"), centralWidget);
    auto* informationLayout = new QGridLayout(informationGroup);

    fileNameValue_ = createValueLabel(informationGroup, "fileNameValue");
    filePathValue_ = createValueLabel(informationGroup, "filePathValue");
    fileSizeValue_ = createValueLabel(informationGroup, "fileSizeValue");
    classValue_ = createValueLabel(informationGroup, "elfClassValue");
    architectureValue_ = createValueLabel(informationGroup, "architectureValue");
    endiannessValue_ = createValueLabel(informationGroup, "endiannessValue");
    entryPointValue_ = createValueLabel(informationGroup, "entryPointValue");
    textAddressValue_ = createValueLabel(informationGroup, "textAddressValue");
    textSizeValue_ = createValueLabel(informationGroup, "textSizeValue");
    sectionCountValue_ = createValueLabel(informationGroup, "sectionCountValue");
    symbolCountValue_ = createValueLabel(informationGroup, "symbolCountValue");
    functionCountValue_ = createValueLabel(informationGroup, "functionCountValue");
    strippedValue_ = createValueLabel(informationGroup, "strippedValue");

    informationLayout->addWidget(new QLabel(tr("File name:"), informationGroup), 0, 0);
    informationLayout->addWidget(fileNameValue_, 0, 1);
    informationLayout->addWidget(new QLabel(tr("File size:"), informationGroup), 0, 2);
    informationLayout->addWidget(fileSizeValue_, 0, 3);
    informationLayout->addWidget(new QLabel(tr("Stripped:"), informationGroup), 0, 4);
    informationLayout->addWidget(strippedValue_, 0, 5);

    informationLayout->addWidget(new QLabel(tr("File path:"), informationGroup), 1, 0);
    informationLayout->addWidget(filePathValue_, 1, 1, 1, 5);

    informationLayout->addWidget(new QLabel(tr("ELF class:"), informationGroup), 2, 0);
    informationLayout->addWidget(classValue_, 2, 1);
    informationLayout->addWidget(new QLabel(tr("Architecture:"), informationGroup), 2, 2);
    informationLayout->addWidget(architectureValue_, 2, 3);
    informationLayout->addWidget(new QLabel(tr("Endianness:"), informationGroup), 2, 4);
    informationLayout->addWidget(endiannessValue_, 2, 5);

    informationLayout->addWidget(new QLabel(tr("Entry point:"), informationGroup), 3, 0);
    informationLayout->addWidget(entryPointValue_, 3, 1);
    informationLayout->addWidget(new QLabel(tr(".text address:"), informationGroup), 3, 2);
    informationLayout->addWidget(textAddressValue_, 3, 3);
    informationLayout->addWidget(new QLabel(tr(".text size:"), informationGroup), 3, 4);
    informationLayout->addWidget(textSizeValue_, 3, 5);

    informationLayout->addWidget(new QLabel(tr("Sections:"), informationGroup), 4, 0);
    informationLayout->addWidget(sectionCountValue_, 4, 1);
    informationLayout->addWidget(new QLabel(tr("Symbols:"), informationGroup), 4, 2);
    informationLayout->addWidget(symbolCountValue_, 4, 3);
    informationLayout->addWidget(new QLabel(tr("Functions:"), informationGroup), 4, 4);
    informationLayout->addWidget(functionCountValue_, 4, 5);
    informationLayout->setColumnStretch(1, 1);
    informationLayout->setColumnStretch(3, 1);
    informationLayout->setColumnStretch(5, 1);

    pageLayout->addWidget(informationGroup);

    auto* analysisSplitter = new QSplitter(Qt::Horizontal, centralWidget);
    analysisSplitter->setObjectName(QStringLiteral("analysisSplitter"));
    analysisSplitter->setChildrenCollapsible(false);

    auto* functionPanel = new QWidget(analysisSplitter);
    functionPanel->setObjectName(QStringLiteral("symbolTreePanel"));
    auto* functionLayout = new QVBoxLayout(functionPanel);
    functionLayout->setContentsMargins(0, 0, 0, 0);
    functionLayout->addWidget(new QLabel(tr("Symbol Tree"), functionPanel));

    functionSearch_ = new QLineEdit(functionPanel);
    functionSearch_->setObjectName(QStringLiteral("functionSearch"));
    functionSearch_->setPlaceholderText(tr("Search symbols or addresses..."));
    functionSearch_->setClearButtonEnabled(true);
    functionLayout->addWidget(functionSearch_);

    functionList_ = new QListWidget(functionPanel);
    functionList_->setObjectName(QStringLiteral("functionList"));
    functionList_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    functionList_->setSelectionMode(QAbstractItemView::SingleSelection);
    functionList_->hide();

    symbolTree_ = new QTreeWidget(functionPanel);
    symbolTree_->setObjectName(QStringLiteral("symbolTree"));
    symbolTree_->setHeaderHidden(true);
    symbolTree_->setRootIsDecorated(true);
    symbolTree_->setAnimated(false);
    symbolTree_->setUniformRowHeights(true);
    symbolTree_->setSelectionMode(QAbstractItemView::SingleSelection);
    symbolTree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    symbolTree_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    symbolTree_->setContextMenuPolicy(Qt::DefaultContextMenu);
    functionLayout->addWidget(symbolTree_, 1);

    auto* analysisTabs = new QTabWidget(analysisSplitter);
    analysisTabs->setObjectName(QStringLiteral("analysisTabs"));

    auto* assemblyGroup = new QGroupBox(tr("Assembly Listing / Control Flow"), analysisTabs);
    assemblyGroup->setObjectName(QStringLiteral("assemblyPanel"));
    auto* assemblyLayout = new QVBoxLayout(assemblyGroup);
    assemblyLayout->setContentsMargins(4, 6, 4, 4);
    assemblyTable_ = new AssemblyGraphTable(assemblyGroup);
    assemblyTable_->setObjectName(QStringLiteral("assemblyTable"));
    assemblyTable_->setColumnCount(4);
    assemblyTable_->setHorizontalHeaderLabels(
        {tr("Flow / Function / Address"), tr("Bytes / Opcode"), tr("Mnemonic"), tr("Operand")});
    assemblyTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    assemblyTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    assemblyTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    assemblyTable_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    assemblyTable_->verticalHeader()->setVisible(false);
    assemblyTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    assemblyTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    assemblyTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    assemblyTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    assemblyTable_->setColumnWidth(0, 340);
    assemblyLayout->addWidget(assemblyTable_);

    callGraphPanel_ = new CallGraphPanel(analysisTabs);
    callGraphView_ = callGraphPanel_->graphView();
    analysisTabs->addTab(assemblyGroup, tr("Assembly"));
    analysisTabs->addTab(callGraphPanel_, tr("Call Graph"));

    auto* pseudocodeGroup = new QGroupBox(tr("Pseudocode"), analysisSplitter);
    pseudocodeGroup->setObjectName(QStringLiteral("pseudocodePanel"));
    auto* pseudocodeLayout = new QVBoxLayout(pseudocodeGroup);
    pseudocodeLayout->setContentsMargins(6, 6, 6, 6);
    auto* pseudocodeSearchLayout = new QHBoxLayout;
    pseudocodeSearch_ = new QLineEdit(pseudocodeGroup);
    pseudocodeSearch_->setObjectName(QStringLiteral("pseudocodeSearch"));
    pseudocodeSearch_->setPlaceholderText(tr("Find in pseudocode..."));
    pseudocodeSearch_->setClearButtonEnabled(true);
    auto* previousMatchButton = new QToolButton(pseudocodeGroup);
    previousMatchButton->setObjectName(QStringLiteral("previousPseudocodeMatchButton"));
    previousMatchButton->setText(QStringLiteral("↑"));
    previousMatchButton->setToolTip(tr("Previous match"));
    auto* nextMatchButton = new QToolButton(pseudocodeGroup);
    nextMatchButton->setObjectName(QStringLiteral("nextPseudocodeMatchButton"));
    nextMatchButton->setText(QStringLiteral("↓"));
    nextMatchButton->setToolTip(tr("Next match"));
    pseudocodeSearchLayout->addWidget(pseudocodeSearch_, 1);
    pseudocodeSearchLayout->addWidget(previousMatchButton);
    pseudocodeSearchLayout->addWidget(nextMatchButton);
    pseudocodeLayout->addLayout(pseudocodeSearchLayout);

    pseudocodeView_ = new PseudocodeView(pseudocodeGroup);
    pseudocodeView_->setObjectName(QStringLiteral("pseudocodeView"));
    pseudocodeView_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    pseudocodeView_->setPlaceholderText(tr("Open a binary to reconstruct pseudocode."));
    new PseudocodeHighlighter(pseudocodeView_->document());
    pseudocodeLayout->addWidget(pseudocodeView_);

    analysisSplitter->addWidget(functionPanel);
    analysisSplitter->addWidget(analysisTabs);
    analysisSplitter->addWidget(pseudocodeGroup);
    analysisSplitter->setStretchFactor(0, 1);
    analysisSplitter->setStretchFactor(1, 4);
    analysisSplitter->setStretchFactor(2, 2);
    analysisSplitter->setSizes({240, 830, 470});
    pageLayout->addWidget(analysisSplitter, 1);
    setCentralWidget(centralWidget);

    connect(
        functionSearch_,
        &QLineEdit::textChanged,
        this,
        &MainWindow::filterFunctions);
    connect(
        functionList_,
        &QListWidget::currentItemChanged,
        this,
        [this](QListWidgetItem* current, QListWidgetItem*) {
            displayFunction(current);
            if(current != nullptr) {
                syncSymbolTreeFunction(current->data(addressRole).toULongLong());
            }
        });
    connect(
        symbolTree_,
        &QTreeWidget::itemClicked,
        this,
        [this](QTreeWidgetItem* item, int) { activateSymbolItem(item); });
    connect(
        assemblyTable_,
        &QTableWidget::cellDoubleClicked,
        this,
        [this](int row, int) { navigateFromAssembly(row); });
    connect(
        assemblyTable_,
        &QTableWidget::currentCellChanged,
        this,
        [this](int currentRow, int, int, int) {
            patchInstructionAction_->setEnabled(
                analysisSession_->isValid() && currentRow >= 0);
            const auto* addressItem = currentRow >= 0
                                          ? assemblyTable_->item(currentRow, 0)
                                          : nullptr;
            if(addressItem == nullptr || functionList_->currentItem() == nullptr) {
                return;
            }
            const auto assemblyFunction = addressItem
                                              ->data(assemblyFunctionAddressRole)
                                              .toULongLong();
            const auto selectedFunction = functionList_->currentItem()
                                              ->data(addressRole)
                                              .toULongLong();
            if(assemblyFunction != selectedFunction) {
                suppressAssemblyFocus_ = true;
                static_cast<void>(selectFunction(assemblyFunction));
                suppressAssemblyFocus_ = false;
            }
        });
    connect(pseudocodeSearch_, &QLineEdit::returnPressed, this, [this] {
        findPseudocodeText(false);
    });
    connect(pseudocodeSearch_, &QLineEdit::textChanged, this, [this](const QString& query) {
        if(!query.isEmpty()) {
            auto cursor = pseudocodeView_->textCursor();
            cursor.movePosition(QTextCursor::Start);
            pseudocodeView_->setTextCursor(cursor);
            findPseudocodeText(false);
        }
    });
    connect(previousMatchButton, &QToolButton::clicked, this, [this] {
        findPseudocodeText(true);
    });
    connect(nextMatchButton, &QToolButton::clicked, this, [this] {
        findPseudocodeText(false);
    });
    pseudocodeView_->setCallActivationHandler(
        [this](std::uint64_t address) { navigateFromPseudocode(address); });
    callGraphPanel_->setNodeActivationHandler(
        [this](std::uint64_t address) { selectFunction(address); });
    callGraphPanel_->setInstructionProvider([this](std::uint64_t address) {
        return analysisSession_->instructionsFor(address);
    });

    analysisProgress_ = new QProgressBar(this);
    analysisProgress_->setObjectName(QStringLiteral("analysisProgress"));
    analysisProgress_->setRange(0, 0);
    analysisProgress_->setMaximumWidth(140);
    analysisProgress_->setTextVisible(false);
    analysisProgress_->hide();
    statusBar()->addPermanentWidget(analysisProgress_);

    clearBinaryInformation();
    clearAnalysisViews();
    resetNavigation();
    statusBar()->showMessage(tr("Ready"));
}

MainWindow::~MainWindow() = default;

bool MainWindow::loadBinary(const std::filesystem::path& path) {
    resetNavigation();
    clearAnalysisViews();
    clearBinaryInformation();
    setWindowTitle(tr("Decompiler"));
    statusBar()->showMessage(tr("Analyzing %1...").arg(QString::fromStdString(path.string())));
    analysisProgress_->show();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    const auto analyzed = analysisSession_->analyze(path);
    QApplication::restoreOverrideCursor();
    analysisProgress_->hide();

    if(!analyzed) {
        clearBinaryInformation();
        setWindowTitle(tr("Decompiler"));
        statusBar()->showMessage(
            tr("Failed to analyze binary: %1")
                .arg(QString::fromStdString(std::string(analysisSession_->errorMessage()))));
        return false;
    }

    updateBinaryInformation();
    populateFunctionList();
    populateSymbolTree();
    populateAssemblyListing();
    updatePseudocodeCallTargets();
    callGraphPanel_->setGraph(analysisSession_->callGraph());

    const auto& metadata = analysisSession_->elfLoader().metadata();
    const auto fileName = QString::fromStdString(metadata.fileName);
    setWindowTitle(tr("Decompiler - %1").arg(fileName));

    const auto mainFunction = std::find_if(
        analysisSession_->functions().begin(),
        analysisSession_->functions().end(),
        [](const FunctionInfo& function) { return function.name == "main"; });
    if(mainFunction != analysisSession_->functions().end()) {
        selectFunction(mainFunction->address);
    } else if(!selectFunction(metadata.entryPoint) && functionList_->count() > 0) {
        functionList_->setCurrentRow(0);
    }

    statusBar()->showMessage(
        tr("Loaded %1 - %2 functions")
            .arg(fileName)
            .arg(static_cast<qulonglong>(analysisSession_->functions().size())));
    return true;
}

const ElfLoader& MainWindow::elfLoader() const noexcept {
    return analysisSession_->elfLoader();
}

const AnalysisSession& MainWindow::analysisSession() const noexcept {
    return *analysisSession_;
}

bool MainWindow::patchInstruction(
    std::uint64_t instructionAddress,
    std::span<const std::uint8_t> replacementBytes,
    const std::filesystem::path& outputPath,
    bool allowOverwrite) {
    if(!analysisSession_->isValid()) {
        statusBar()->showMessage(tr("Open and analyze a binary before patching."));
        return false;
    }

    const Instruction* selectedInstruction = nullptr;
    for(const auto& function : analysisSession_->functions()) {
        const auto* instructions = analysisSession_->instructionsFor(function.address);
        if(instructions == nullptr) {
            continue;
        }
        const auto instruction = std::find_if(
            instructions->begin(), instructions->end(), [instructionAddress](const auto& value) {
                return value.address == instructionAddress;
            });
        if(instruction != instructions->end()) {
            selectedInstruction = &*instruction;
            break;
        }
    }

    if(selectedInstruction == nullptr) {
        statusBar()->showMessage(tr("The selected instruction is not part of the analysis."));
        return false;
    }

    const BinaryPatcher patcher;
    const auto result = patcher.patchInstruction(
        analysisSession_->elfLoader(),
        selectedInstruction->address,
        selectedInstruction->bytes,
        replacementBytes,
        outputPath,
        allowOverwrite);
    if(!result.succeeded()) {
        statusBar()->showMessage(
            tr("Patch rejected: %1").arg(QString::fromStdString(result.errorMessage)));
        return false;
    }

    if(!loadBinary(result.outputPath)) {
        return false;
    }
    statusBar()->showMessage(
        tr("Patched binary saved and re-analyzed: %1")
            .arg(QString::fromStdString(result.outputPath.string())));
    return true;
}

void MainWindow::chooseBinary() {
    const auto fileName = QFileDialog::getOpenFileName(
        this,
        tr("Open ELF Binary"),
        {},
        tr("ELF binaries (*)"));
    if(fileName.isEmpty()) {
        return;
    }

    if(!loadBinary(std::filesystem::path(fileName.toStdString()))) {
        QMessageBox::critical(
            this,
            tr("Unable to Open Binary"),
            QString::fromStdString(std::string(analysisSession_->errorMessage())));
    }
}

void MainWindow::patchSelectedInstruction() {
    const auto row = assemblyTable_->currentRow();
    const auto* addressItem = row >= 0 ? assemblyTable_->item(row, 0) : nullptr;
    if(addressItem == nullptr) {
        QMessageBox::information(this, tr("Patch Instruction"), tr("Select an instruction first."));
        return;
    }

    const auto address = addressItem->data(addressRole).toULongLong();
    const Instruction* selectedInstruction = nullptr;
    for(const auto& function : analysisSession_->functions()) {
        const auto* instructions = analysisSession_->instructionsFor(function.address);
        if(instructions == nullptr) {
            continue;
        }
        const auto instruction = std::find_if(
            instructions->begin(), instructions->end(), [address](const auto& value) {
                return value.address == address;
            });
        if(instruction != instructions->end()) {
            selectedInstruction = &*instruction;
            break;
        }
    }
    if(selectedInstruction == nullptr) {
        QMessageBox::critical(
            this, tr("Patch Instruction"), tr("Cached instruction bytes are unavailable."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Patch Instruction"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    form->addRow(tr("Address:"), new QLabel(hexadecimal(address), &dialog));
    form->addRow(
        tr("Original bytes:"),
        new QLabel(byteString(selectedInstruction->bytes), &dialog));
    auto* replacementInput = new QLineEdit(byteString(selectedInstruction->bytes), &dialog);
    replacementInput->setObjectName(QStringLiteral("patchBytesInput"));
    replacementInput->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    form->addRow(tr("Replacement hex:"), replacementInput);
    layout->addLayout(form);

    auto* nopButton = new QToolButton(&dialog);
    nopButton->setObjectName(QStringLiteral("fillNopButton"));
    nopButton->setText(tr("Fill with NOP"));
    layout->addWidget(nopButton);
    connect(nopButton, &QToolButton::clicked, &dialog, [replacementInput, selectedInstruction] {
        replacementInput->setText(byteString(BinaryPatcher::nopBytes(selectedInstruction->bytes.size())));
    });

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if(dialog.exec() != QDialog::Accepted) {
        return;
    }

    const auto parsed = BinaryPatcher::parseHexBytes(replacementInput->text().toStdString());
    if(!parsed.succeeded()) {
        QMessageBox::critical(
            this,
            tr("Invalid Patch"),
            QString::fromStdString(parsed.errorMessage));
        return;
    }
    if(parsed.bytes.size() != selectedInstruction->bytes.size()) {
        QMessageBox::critical(
            this,
            tr("Invalid Patch"),
            tr("Replacement must contain exactly %1 bytes.")
                .arg(static_cast<qulonglong>(selectedInstruction->bytes.size())));
        return;
    }

    const auto& sourcePath = analysisSession_->elfLoader().metadata().filePath;
    const auto suggestedPath =
        sourcePath.parent_path()
        / (sourcePath.stem().string() + "-patched" + sourcePath.extension().string());
    const auto selectedPath = QFileDialog::getSaveFileName(
        this,
        tr("Save Patched Binary"),
        QString::fromStdString(suggestedPath.string()),
        tr("ELF binaries (*)"));
    if(selectedPath.isEmpty()) {
        return;
    }

    const auto outputPath = std::filesystem::path(selectedPath.toStdString());
    std::error_code filesystemError;
    const bool outputExists = std::filesystem::exists(outputPath, filesystemError);
    bool allowOverwrite = false;
    if(!filesystemError && outputExists) {
        allowOverwrite = QMessageBox::question(
                             this,
                             tr("Confirm Overwrite"),
                             tr("The selected file already exists. Overwrite it?"),
                             QMessageBox::Yes | QMessageBox::No,
                             QMessageBox::No)
                         == QMessageBox::Yes;
        if(!allowOverwrite) {
            return;
        }
    }

    if(!patchInstruction(address, parsed.bytes, outputPath, allowOverwrite)) {
        QMessageBox::critical(this, tr("Patch Failed"), statusBar()->currentMessage());
    }
}

void MainWindow::clearBinaryInformation() {
    const auto emptyValue = tr("Not loaded");
    const auto labels = {
        fileNameValue_,
        filePathValue_,
        fileSizeValue_,
        classValue_,
        architectureValue_,
        endiannessValue_,
        entryPointValue_,
        textAddressValue_,
        textSizeValue_,
        sectionCountValue_,
        symbolCountValue_,
        functionCountValue_,
        strippedValue_,
    };

    for(auto* label : labels) {
        label->setText(emptyValue);
    }
}

void MainWindow::clearAnalysisViews() {
    functionSearch_->clear();
    pseudocodeSearch_->clear();
    functionList_->clear();
    symbolTree_->clear();
    symbolTreeFunctionItems_.clear();
    pseudocodeView_->setCallTargets({});
    pseudocodeView_->clear();
    callGraphPanel_->clearGraph();
    assemblyTable_->clearFlowEdges();
    assemblyTable_->clearContents();
    assemblyTable_->setRowCount(0);
    assemblyFunctionRows_.clear();
    assemblyFunctionInstructionCounts_.clear();
    preserveAssemblyEntryOnNextSelection_ = false;
    suppressAssemblyFocus_ = false;
    patchInstructionAction_->setEnabled(false);
}

void MainWindow::updateBinaryInformation() {
    const auto& loader = analysisSession_->elfLoader();
    const auto& metadata = loader.metadata();
    const auto textSection = loader.findSection(".text");

    fileNameValue_->setText(QString::fromStdString(metadata.fileName));
    filePathValue_->setText(QString::fromStdString(metadata.filePath.string()));
    fileSizeValue_->setText(
        tr("%1 bytes").arg(static_cast<qulonglong>(metadata.fileSize)));
    classValue_->setText(
        metadata.is64Bit
            ? (metadata.isPositionIndependent ? tr("ELF64 PIE") : tr("ELF64"))
            : tr("Unsupported"));
    architectureValue_->setText(tr("x86-64"));
    endiannessValue_->setText(
        metadata.isLittleEndian ? tr("Little endian") : tr("Unsupported"));
    entryPointValue_->setText(hexadecimal(metadata.entryPoint));
    textAddressValue_->setText(textSection ? hexadecimal(textSection->address) : tr("Unavailable"));
    textSizeValue_->setText(
        textSection ? tr("%1 bytes").arg(static_cast<qulonglong>(textSection->size))
                    : tr("Unavailable"));
    sectionCountValue_->setText(QString::number(metadata.sectionCount));
    symbolCountValue_->setText(QString::number(metadata.symbolCount));
    functionCountValue_->setText(QString::number(analysisSession_->functions().size()));
    strippedValue_->setText(metadata.isStripped ? tr("Yes") : tr("No"));
}

void MainWindow::populateFunctionList() {
    functionList_->setUpdatesEnabled(false);
    functionList_->clear();

    for(const auto& function : analysisSession_->functions()) {
        const auto name = QString::fromStdString(function.name);
        auto* item = new QListWidgetItem(
            tr("%1  %2  (%3 bytes)")
                .arg(name)
                .arg(hexadecimal(function.address))
                .arg(static_cast<qulonglong>(function.size)),
            functionList_);
        item->setData(addressRole, static_cast<qulonglong>(function.address));
        item->setData(functionNameRole, name);
        item->setToolTip(
            tr("Source: %1%2")
                .arg(sourceDescription(function.source))
                .arg(function.sizeIsEstimated ? tr("\nSize is estimated") : QString {}));
    }

    functionList_->setUpdatesEnabled(true);
}

void MainWindow::populateSymbolTree() {
    symbolTree_->setUpdatesEnabled(false);
    symbolTree_->clear();
    symbolTreeFunctionItems_.clear();

    const auto folderIcon = style()->standardIcon(QStyle::SP_DirClosedIcon);
    const auto functionIcon = style()->standardIcon(QStyle::SP_ArrowRight);
    const auto importIcon = style()->standardIcon(QStyle::SP_ArrowDown);
    const auto exportIcon = style()->standardIcon(QStyle::SP_ArrowUp);
    const auto symbolIcon = style()->standardIcon(QStyle::SP_FileIcon);

    const auto addCategory = [this, &folderIcon](const QString& name, const char* key) {
        auto* item = new QTreeWidgetItem(symbolTree_, QStringList {name});
        item->setIcon(0, folderIcon);
        item->setData(
            0,
            symbolItemKindRole,
            static_cast<int>(SymbolTreeItemKind::Category));
        item->setData(0, symbolCategoryRole, QString::fromLatin1(key));
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        return item;
    };
    const auto addSymbol = [](
                               QTreeWidgetItem* parent,
                               const QIcon& icon,
                               const QString& name,
                               std::uint64_t address,
                               SymbolTreeItemKind kind,
                               const QString& toolTip) {
        const auto text = address == 0
                              ? name
                              : QStringLiteral("%1  %2").arg(name, hexadecimal(address));
        auto* item = new QTreeWidgetItem(parent, QStringList {text});
        item->setIcon(0, icon);
        item->setData(0, symbolItemKindRole, static_cast<int>(kind));
        item->setData(0, functionNameRole, name);
        if(address != 0) {
            item->setData(0, addressRole, static_cast<qulonglong>(address));
        }
        item->setToolTip(0, toolTip);
        return item;
    };
    const auto symbolTypeName = [](std::uint8_t type) {
        switch(type) {
        case STT_NOTYPE:
            return QStringLiteral("label");
        case STT_OBJECT:
            return QStringLiteral("object");
        case STT_FUNC:
            return QStringLiteral("function");
        case STT_SECTION:
            return QStringLiteral("section");
        case STT_FILE:
            return QStringLiteral("file");
        case STT_TLS:
            return QStringLiteral("TLS object");
        default:
            return QStringLiteral("symbol");
        }
    };

    auto* importsCategory = addCategory(tr("Imports"), "imports");
    auto* exportsCategory = addCategory(tr("Exports"), "exports");
    auto* functionsCategory = addCategory(tr("Functions"), "functions");
    auto* labelsCategory = addCategory(tr("Labels"), "labels");
    auto* dataCategory = addCategory(tr("Data"), "data");
    auto* sectionsCategory = addCategory(tr("Sections"), "sections");
    auto* classesCategory = addCategory(tr("Classes"), "classes");
    auto* namespacesCategory = addCategory(tr("Namespaces"), "namespaces");

    for(const auto& function : analysisSession_->functions()) {
        const auto name = QString::fromStdString(function.name);
        auto* item = addSymbol(
            functionsCategory,
            functionIcon,
            name,
            function.address,
            SymbolTreeItemKind::Function,
            tr("Function at %1\nSource: %2%3")
                .arg(hexadecimal(function.address))
                .arg(sourceDescription(function.source))
                .arg(function.sizeIsEstimated ? tr("\nSize is estimated") : QString {}));
        symbolTreeFunctionItems_.insert_or_assign(function.address, item);
    }

    std::set<std::pair<std::string, std::uint64_t>> importedSymbols;
    std::set<std::pair<std::string, std::uint64_t>> exportedSymbols;
    std::set<std::pair<std::string, std::uint64_t>> labels;
    std::set<std::pair<std::string, std::uint64_t>> dataSymbols;
    for(const auto& symbol : analysisSession_->elfLoader().symbols()) {
        if(symbol.name.empty()) {
            continue;
        }
        const auto name = QString::fromStdString(symbol.name);
        const auto toolTip = tr("%1 symbol%2\nSize: %3 bytes\nTable: %4")
                                 .arg(symbolTypeName(symbol.type))
                                 .arg(symbol.address == 0
                                          ? QString {}
                                          : tr(" at %1").arg(hexadecimal(symbol.address)))
                                 .arg(static_cast<qulonglong>(symbol.size))
                                 .arg(symbol.fromDynamicTable
                                          ? tr("dynamic")
                                          : tr("static"));
        const auto key = std::pair {symbol.name, symbol.address};
        if(symbol.sectionIndex == SHN_UNDEF) {
            if(importedSymbols.insert(key).second) {
                addSymbol(
                    importsCategory,
                    importIcon,
                    name,
                    0,
                    SymbolTreeItemKind::Import,
                    toolTip);
            }
            continue;
        }

        if((symbol.binding == STB_GLOBAL || symbol.binding == STB_WEAK)
           && exportedSymbols.insert(key).second) {
            addSymbol(
                exportsCategory,
                exportIcon,
                name,
                symbol.address,
                symbol.type == STT_FUNC ? SymbolTreeItemKind::CodeSymbol
                                        : SymbolTreeItemKind::DataSymbol,
                toolTip);
        }
        if(symbol.type == STT_NOTYPE && symbol.address != 0
           && labels.insert(key).second) {
            addSymbol(
                labelsCategory,
                symbolIcon,
                name,
                symbol.address,
                SymbolTreeItemKind::CodeSymbol,
                toolTip);
        } else if((symbol.type == STT_OBJECT || symbol.type == STT_TLS)
                  && symbol.address != 0 && dataSymbols.insert(key).second) {
            addSymbol(
                dataCategory,
                symbolIcon,
                name,
                symbol.address,
                SymbolTreeItemKind::DataSymbol,
                toolTip);
        }
    }

    for(const auto& section : analysisSession_->elfLoader().sections()) {
        if(section.name.empty()) {
            continue;
        }
        addSymbol(
            sectionsCategory,
            symbolIcon,
            QString::fromStdString(section.name),
            section.address,
            SymbolTreeItemKind::Section,
            tr("Section %1\nAddress: %2\nSize: %3 bytes")
                .arg(QString::fromStdString(section.name))
                .arg(hexadecimal(section.address))
                .arg(static_cast<qulonglong>(section.size)));
    }

    const auto setCategorySummary = [this](QTreeWidgetItem* category) {
        category->setToolTip(
            0,
            tr("%1 item(s)").arg(category->childCount()));
    };
    for(auto* category : {
            importsCategory,
            exportsCategory,
            functionsCategory,
            labelsCategory,
            dataCategory,
            sectionsCategory,
            classesCategory,
            namespacesCategory,
        }) {
        setCategorySummary(category);
    }
    classesCategory->setToolTip(
        0,
        tr("Class information is unavailable without C++ type metadata."));
    namespacesCategory->setToolTip(
        0,
        tr("Namespace information is unavailable without demangled C++ symbols."));

    functionsCategory->setExpanded(true);
    symbolTree_->setUpdatesEnabled(true);
}

void MainWindow::populateAssemblyListing() {
    assemblyTable_->setUpdatesEnabled(false);
    assemblyTable_->clearFlowEdges();
    assemblyTable_->clearContents();
    assemblyTable_->setRowCount(0);
    assemblyFunctionRows_.clear();
    assemblyFunctionInstructionCounts_.clear();

    std::vector<const FunctionInfo*> orderedFunctions;
    orderedFunctions.reserve(analysisSession_->functions().size());
    for(const auto& function : analysisSession_->functions()) {
        const auto* instructions = analysisSession_->instructionsFor(function.address);
        if(instructions != nullptr && !instructions->empty()) {
            orderedFunctions.push_back(&function);
        }
    }
    std::sort(
        orderedFunctions.begin(),
        orderedFunctions.end(),
        [](const FunctionInfo* left, const FunctionInfo* right) {
            return left->address < right->address;
        });

    const auto entryPoint = analysisSession_->elfLoader().metadata().entryPoint;
    const auto entryFunction = std::find_if(
        orderedFunctions.begin(), orderedFunctions.end(), [entryPoint](const FunctionInfo* function) {
            return function->address == entryPoint || function->source == FunctionSource::EntryPoint;
        });
    if(entryFunction != orderedFunctions.end() && entryFunction != orderedFunctions.begin()) {
        std::rotate(orderedFunctions.begin(), entryFunction, std::next(entryFunction));
    }

    std::size_t instructionCount = 0;
    for(const auto* function : orderedFunctions) {
        instructionCount += analysisSession_->instructionsFor(function->address)->size();
    }
    assemblyTable_->setRowCount(static_cast<int>(instructionCount));

    std::unordered_map<std::uint64_t, int> instructionRows;
    instructionRows.reserve(instructionCount);
    struct PendingFlowEdge {
        int sourceRow = -1;
        std::uint64_t targetAddress = 0;
        InstructionKind kind = InstructionKind::Normal;
    };
    std::vector<PendingFlowEdge> pendingFlowEdges;

    int row = 0;
    const QString gutterIndent(16, QLatin1Char(' '));
    for(const auto* function : orderedFunctions) {
        const auto* instructions = analysisSession_->instructionsFor(function->address);
        assemblyFunctionRows_.insert_or_assign(function->address, row);
        assemblyFunctionInstructionCounts_.insert_or_assign(
            function->address, instructions->size());

        for(std::size_t index = 0; index < instructions->size(); ++index, ++row) {
            const auto& instruction = instructions->at(index);
            auto addressText = gutterIndent + hexadecimal(instruction.address);
            if(index == 0) {
                const auto functionName = function->name.empty()
                                              ? QStringLiteral("sub_%1").arg(
                                                    QString::number(function->address, 16))
                                              : QString::fromStdString(function->name);
                addressText = gutterIndent + functionName + QStringLiteral(":  ")
                              + hexadecimal(instruction.address);
            }

            auto* addressItem = new QTableWidgetItem(addressText);
            auto* bytesItem = new QTableWidgetItem(byteString(instruction.bytes));
            auto* mnemonicItem = new QTableWidgetItem(
                QString::fromStdString(instruction.mnemonic));
            auto* operandItem = new QTableWidgetItem(
                QString::fromStdString(instruction.operandText));

            addressItem->setData(addressRole, static_cast<qulonglong>(instruction.address));
            addressItem->setData(
                assemblyFunctionAddressRole, static_cast<qulonglong>(function->address));
            addressItem->setData(instructionKindRole, static_cast<int>(instruction.kind));
            if(instruction.directTarget) {
                addressItem->setData(
                    directTargetRole, static_cast<qulonglong>(*instruction.directTarget));
            }

            const auto toolTip = tr("%1 at %2")
                                     .arg(QString::fromStdString(function->name))
                                     .arg(hexadecimal(function->address));
            addressItem->setToolTip(toolTip);
            bytesItem->setToolTip(toolTip);
            mnemonicItem->setToolTip(toolTip);
            operandItem->setToolTip(toolTip);

            if(index == 0) {
                auto functionFont = addressItem->font();
                functionFont.setBold(true);
                addressItem->setFont(functionFont);
                const QBrush functionBackground(QColor(QStringLiteral("#EEEEEE")));
                addressItem->setBackground(functionBackground);
                bytesItem->setBackground(functionBackground);
                mnemonicItem->setBackground(functionBackground);
                operandItem->setBackground(functionBackground);
            }

            const auto color = instructionColor(instruction.kind);
            if(color.isValid()) {
                const QBrush brush(color);
                addressItem->setForeground(brush);
                bytesItem->setForeground(brush);
                mnemonicItem->setForeground(brush);
                operandItem->setForeground(brush);
            }

            assemblyTable_->setItem(row, 0, addressItem);
            assemblyTable_->setItem(row, 1, bytesItem);
            assemblyTable_->setItem(row, 2, mnemonicItem);
            assemblyTable_->setItem(row, 3, operandItem);
            instructionRows.insert_or_assign(instruction.address, row);

            if(instruction.directTarget
               && (instruction.kind == InstructionKind::ConditionalJump
                   || instruction.kind == InstructionKind::UnconditionalJump)) {
                pendingFlowEdges.push_back(PendingFlowEdge {
                    .sourceRow = row,
                    .targetAddress = *instruction.directTarget,
                    .kind = instruction.kind,
                });
            }
        }
    }

    std::vector<AssemblyFlowEdge> flowEdges;
    flowEdges.reserve(pendingFlowEdges.size());
    for(const auto& pending : pendingFlowEdges) {
        const auto target = instructionRows.find(pending.targetAddress);
        if(target != instructionRows.end()) {
            flowEdges.push_back(AssemblyFlowEdge {
                .sourceRow = pending.sourceRow,
                .targetRow = target->second,
                .kind = pending.kind,
            });
        }
    }
    assemblyTable_->setFlowEdges(std::move(flowEdges));
    assemblyTable_->setColumnWidth(0, 340);
    assemblyTable_->setUpdatesEnabled(true);
    assemblyTable_->scrollToTop();
    preserveAssemblyEntryOnNextSelection_ = true;
}

void MainWindow::focusAssemblyFunction(std::uint64_t address) {
    const auto row = assemblyFunctionRows_.find(address);
    if(row == assemblyFunctionRows_.end()) {
        return;
    }
    assemblyTable_->setCurrentCell(row->second, 0);
    if(auto* item = assemblyTable_->item(row->second, 0)) {
        assemblyTable_->scrollToItem(item, QAbstractItemView::PositionAtTop);
    }
}

void MainWindow::focusAssemblyAddress(std::uint64_t address) {
    for(int row = 0; row < assemblyTable_->rowCount(); ++row) {
        const auto* item = assemblyTable_->item(row, 0);
        if(item == nullptr || item->data(addressRole).toULongLong() != address) {
            continue;
        }
        assemblyTable_->setCurrentCell(row, 0);
        assemblyTable_->scrollToItem(item, QAbstractItemView::PositionAtCenter);
        return;
    }
}

void MainWindow::activateSymbolItem(QTreeWidgetItem* item) {
    if(item == nullptr || !item->data(0, symbolItemKindRole).isValid()) {
        return;
    }
    const auto kind = static_cast<SymbolTreeItemKind>(
        item->data(0, symbolItemKindRole).toInt());
    if(kind == SymbolTreeItemKind::Category) {
        item->setExpanded(!item->isExpanded());
        return;
    }

    const auto name = item->data(0, functionNameRole).toString();
    if(kind == SymbolTreeItemKind::Import) {
        statusBar()->showMessage(tr("Imported symbol: %1").arg(name));
        return;
    }
    if(!item->data(0, addressRole).isValid()) {
        return;
    }

    const auto address = item->data(0, addressRole).toULongLong();
    if(kind == SymbolTreeItemKind::Function) {
        static_cast<void>(selectFunction(address));
        return;
    }

    const FunctionInfo* containingFunction = analysisSession_->functionAt(address);
    if(containingFunction == nullptr && kind == SymbolTreeItemKind::CodeSymbol) {
        const auto function = std::find_if(
            analysisSession_->functions().begin(),
            analysisSession_->functions().end(),
            [address](const FunctionInfo& candidate) {
                return address >= candidate.address
                       && address - candidate.address < candidate.size;
            });
        if(function != analysisSession_->functions().end()) {
            containingFunction = &*function;
        }
    }
    if(containingFunction != nullptr) {
        static_cast<void>(selectFunction(containingFunction->address));
        focusAssemblyAddress(address);
        statusBar()->showMessage(
            tr("Symbol %1 at %2").arg(name, hexadecimal(address)));
        return;
    }

    statusBar()->showMessage(
        tr("Symbol %1 at %2 is outside decoded code.")
            .arg(name, hexadecimal(address)));
}

void MainWindow::syncSymbolTreeFunction(std::uint64_t address) {
    const auto item = symbolTreeFunctionItems_.find(address);
    if(item == symbolTreeFunctionItems_.end()) {
        return;
    }
    const QSignalBlocker blocker(symbolTree_);
    if(item->second->parent() != nullptr) {
        item->second->parent()->setExpanded(true);
    }
    symbolTree_->setCurrentItem(item->second);
    symbolTree_->scrollToItem(item->second, QAbstractItemView::PositionAtCenter);
}

void MainWindow::filterFunctions(const QString& query) {
    const auto normalizedQuery = query.trimmed().toLower();
    QListWidgetItem* firstVisible = nullptr;

    for(int row = 0; row < functionList_->count(); ++row) {
        auto* item = functionList_->item(row);
        const auto name = item->data(functionNameRole).toString().toLower();
        const auto address = item->data(addressRole).toULongLong();
        const auto formattedAddress = hexadecimal(address).toLower();
        const auto addressWithoutPrefix = formattedAddress.mid(2);
        const bool matches = normalizedQuery.isEmpty() || name.contains(normalizedQuery)
                             || formattedAddress.contains(normalizedQuery)
                             || addressWithoutPrefix.contains(normalizedQuery);
        item->setHidden(!matches);
        if(matches && firstVisible == nullptr) {
            firstVisible = item;
        }
    }

    if(functionList_->currentItem() != nullptr
       && functionList_->currentItem()->isHidden() && firstVisible != nullptr) {
        functionList_->setCurrentItem(firstVisible);
    }

    for(int categoryIndex = 0; categoryIndex < symbolTree_->topLevelItemCount();
        ++categoryIndex) {
        auto* category = symbolTree_->topLevelItem(categoryIndex);
        const auto categoryMatches = !normalizedQuery.isEmpty()
                                     && category->text(0).toLower().contains(normalizedQuery);
        auto visibleChildren = 0;
        for(int childIndex = 0; childIndex < category->childCount(); ++childIndex) {
            auto* child = category->child(childIndex);
            const auto name = child->data(0, functionNameRole).toString().toLower();
            const auto address = child->data(0, addressRole).toULongLong();
            const auto formattedAddress = address == 0
                                              ? QString {}
                                              : hexadecimal(address).toLower();
            const auto matches = normalizedQuery.isEmpty() || categoryMatches
                                 || name.contains(normalizedQuery)
                                 || child->text(0).toLower().contains(normalizedQuery)
                                 || formattedAddress.contains(normalizedQuery)
                                 || (formattedAddress.size() > 2
                                     && formattedAddress.mid(2).contains(normalizedQuery));
            child->setHidden(!matches);
            visibleChildren += matches ? 1 : 0;
        }
        const auto showEmptyMatchingCategory = categoryMatches && category->childCount() == 0;
        category->setHidden(
            !normalizedQuery.isEmpty() && visibleChildren == 0
            && !showEmptyMatchingCategory);
        if(!normalizedQuery.isEmpty() && visibleChildren > 0) {
            category->setExpanded(true);
        }
    }
}

void MainWindow::displayFunction(QListWidgetItem* item) {
    pseudocodeView_->clear();
    if(item == nullptr) {
        return;
    }

    const auto functionAddress = item->data(addressRole).toULongLong();
    const auto* function = analysisSession_->functionAt(functionAddress);
    const auto* instructions = analysisSession_->instructionsFor(functionAddress);
    const auto* pseudocode = analysisSession_->pseudocodeFor(functionAddress);
    if(function == nullptr || instructions == nullptr || pseudocode == nullptr) {
        statusBar()->showMessage(tr("No cached analysis is available for this function."));
        return;
    }

    recordNavigation(functionAddress);
    callGraphPanel_->setActiveFunction(functionAddress);
    pseudocodeView_->setPlainText(QString::fromStdString(*pseudocode));
    if(preserveAssemblyEntryOnNextSelection_) {
        preserveAssemblyEntryOnNextSelection_ = false;
    } else if(!suppressAssemblyFocus_) {
        focusAssemblyFunction(functionAddress);
    }
    if(!pseudocodeSearch_->text().isEmpty()) {
        auto cursor = pseudocodeView_->textCursor();
        cursor.movePosition(QTextCursor::Start);
        pseudocodeView_->setTextCursor(cursor);
        findPseudocodeText(false);
    }

    statusBar()->showMessage(
        tr("%1 at %2 - %3 instructions")
            .arg(QString::fromStdString(function->name))
            .arg(hexadecimal(function->address))
            .arg(static_cast<qulonglong>(instructions->size())));
}

void MainWindow::navigateFromAssembly(int row) {
    auto resolvedRow = row;
    if(functionList_->currentItem() != nullptr) {
        const auto currentFunction = functionList_->currentItem()
                                         ->data(addressRole)
                                         .toULongLong();
        const auto start = assemblyFunctionRows_.find(currentFunction);
        const auto count = assemblyFunctionInstructionCounts_.find(currentFunction);
        if(start != assemblyFunctionRows_.end()
           && count != assemblyFunctionInstructionCounts_.end()
           && !(row >= start->second
                && row < start->second + static_cast<int>(count->second))
           && row >= 0 && static_cast<std::size_t>(row) < count->second) {
            resolvedRow = start->second + row;
        }
    }

    const auto* addressItem = assemblyTable_->item(resolvedRow, 0);
    if(addressItem == nullptr || !addressItem->data(directTargetRole).isValid()) {
        return;
    }

    const auto target = addressItem->data(directTargetRole).toULongLong();
    const auto kind = static_cast<InstructionKind>(
        addressItem->data(instructionKindRole).toInt());
    if(kind == InstructionKind::Call) {
        if(!selectFunction(target)) {
            statusBar()->showMessage(
                tr("Direct call target %1 is outside the discovered functions.")
                    .arg(hexadecimal(target)));
        }
        return;
    }

    if(kind != InstructionKind::ConditionalJump
       && kind != InstructionKind::UnconditionalJump) {
        return;
    }

    for(int targetRow = 0; targetRow < assemblyTable_->rowCount(); ++targetRow) {
        const auto* candidate = assemblyTable_->item(targetRow, 0);
        if(candidate != nullptr && candidate->data(addressRole).toULongLong() == target) {
            assemblyTable_->selectRow(targetRow);
            assemblyTable_->scrollToItem(candidate, QAbstractItemView::PositionAtCenter);
            return;
        }
    }

    statusBar()->showMessage(
        tr("Branch target %1 is outside the analyzed assembly listing.")
            .arg(hexadecimal(target)));
}

void MainWindow::navigateFromPseudocode(std::uint64_t address) {
    if(!selectFunction(address)) {
        statusBar()->showMessage(
            tr("Pseudocode call target %1 is outside the discovered functions.")
                .arg(hexadecimal(address)));
    }
}

void MainWindow::navigateBack() {
    if(navigationIndex_ <= 0) {
        return;
    }

    --navigationIndex_;
    restoringNavigation_ = true;
    const auto selected = selectFunction(
        navigationHistory_[static_cast<std::size_t>(navigationIndex_)]);
    restoringNavigation_ = false;
    if(!selected) {
        navigationHistory_.clear();
        navigationIndex_ = -1;
    }
    updateNavigationActions();
}

void MainWindow::navigateForward() {
    if(navigationIndex_ < 0
       || static_cast<std::size_t>(navigationIndex_ + 1) >= navigationHistory_.size()) {
        return;
    }

    ++navigationIndex_;
    restoringNavigation_ = true;
    const auto selected = selectFunction(
        navigationHistory_[static_cast<std::size_t>(navigationIndex_)]);
    restoringNavigation_ = false;
    if(!selected) {
        navigationHistory_.clear();
        navigationIndex_ = -1;
    }
    updateNavigationActions();
}

void MainWindow::recordNavigation(std::uint64_t address) {
    if(restoringNavigation_) {
        return;
    }
    if(navigationIndex_ >= 0
       && navigationHistory_[static_cast<std::size_t>(navigationIndex_)] == address) {
        return;
    }

    const auto firstForwardEntry = navigationIndex_ + 1;
    if(firstForwardEntry >= 0
       && static_cast<std::size_t>(firstForwardEntry) < navigationHistory_.size()) {
        navigationHistory_.erase(
            navigationHistory_.begin() + firstForwardEntry,
            navigationHistory_.end());
    }
    navigationHistory_.push_back(address);
    navigationIndex_ = static_cast<std::ptrdiff_t>(navigationHistory_.size()) - 1;
    updateNavigationActions();
}

void MainWindow::resetNavigation() {
    navigationHistory_.clear();
    navigationIndex_ = -1;
    restoringNavigation_ = false;
    updateNavigationActions();
}

void MainWindow::updateNavigationActions() {
    if(backAction_ == nullptr || forwardAction_ == nullptr) {
        return;
    }
    backAction_->setEnabled(navigationIndex_ > 0);
    forwardAction_->setEnabled(
        navigationIndex_ >= 0
        && static_cast<std::size_t>(navigationIndex_ + 1) < navigationHistory_.size());
}

void MainWindow::updatePseudocodeCallTargets() {
    std::unordered_map<std::string, std::uint64_t> targets;
    targets.reserve(analysisSession_->functions().size());
    for(const auto& function : analysisSession_->functions()) {
        targets.insert_or_assign(
            PseudocodeGenerator::identifierForFunction(function.name, function.address),
            function.address);
    }
    pseudocodeView_->setCallTargets(std::move(targets));
}

void MainWindow::findPseudocodeText(bool backward) {
    const auto query = pseudocodeSearch_->text();
    if(query.isEmpty() || pseudocodeView_->document()->isEmpty()) {
        return;
    }

    auto flags = QTextDocument::FindFlags {};
    if(backward) {
        flags |= QTextDocument::FindBackward;
    }
    if(!pseudocodeView_->find(query, flags)) {
        auto cursor = pseudocodeView_->textCursor();
        cursor.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
        pseudocodeView_->setTextCursor(cursor);
        if(!pseudocodeView_->find(query, flags)) {
            statusBar()->showMessage(tr("No pseudocode match for “%1”.").arg(query));
            return;
        }
    }
    statusBar()->showMessage(tr("Pseudocode match: %1").arg(query));
}

bool MainWindow::selectFunction(std::uint64_t address) {
    for(int row = 0; row < functionList_->count(); ++row) {
        auto* item = functionList_->item(row);
        if(item->data(addressRole).toULongLong() != address) {
            continue;
        }

        if(item->isHidden()) {
            functionSearch_->clear();
        }
        functionList_->setCurrentItem(item);
        functionList_->scrollToItem(item, QAbstractItemView::PositionAtCenter);
        return true;
    }
    return false;
}

} // namespace decompiler
