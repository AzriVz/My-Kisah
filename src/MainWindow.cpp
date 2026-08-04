#include "MainWindow.hpp"

#include "AnalysisSession.hpp"
#include "BinaryPatcher.hpp"
#include "CallGraphPanel.hpp"
#include "CallGraphView.hpp"
#include "ElfLoader.hpp"
#include "FunctionInfo.hpp"
#include "Instruction.hpp"
#include "PseudocodeGenerator.hpp"
#include "PseudocodeHighlighter.hpp"
#include "PseudocodeView.hpp"

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
#include <QSplitter>
#include <QStatusBar>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>

static constexpr int addressRole = Qt::UserRole + 1;
static constexpr int directTargetRole = Qt::UserRole + 2;
static constexpr int instructionKindRole = Qt::UserRole + 3;
static constexpr int functionNameRole = Qt::UserRole + 4;

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
    resize(1180, 760);

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

    auto* functionPanel = new QWidget(analysisSplitter);
    auto* functionLayout = new QVBoxLayout(functionPanel);
    functionLayout->setContentsMargins(0, 0, 0, 0);
    functionLayout->addWidget(new QLabel(tr("Functions"), functionPanel));

    functionSearch_ = new QLineEdit(functionPanel);
    functionSearch_->setObjectName(QStringLiteral("functionSearch"));
    functionSearch_->setPlaceholderText(tr("Search name or address..."));
    functionSearch_->setClearButtonEnabled(true);
    functionLayout->addWidget(functionSearch_);

    functionList_ = new QListWidget(functionPanel);
    functionList_->setObjectName(QStringLiteral("functionList"));
    functionList_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    functionList_->setSelectionMode(QAbstractItemView::SingleSelection);
    functionLayout->addWidget(functionList_, 1);

    auto* detailSplitter = new QSplitter(Qt::Vertical, analysisSplitter);
    detailSplitter->setObjectName(QStringLiteral("detailSplitter"));

    auto* analysisTabs = new QTabWidget(detailSplitter);
    analysisTabs->setObjectName(QStringLiteral("analysisTabs"));

    auto* pseudocodeGroup = new QGroupBox(tr("Reconstructed Pseudocode"), analysisTabs);
    auto* pseudocodeLayout = new QVBoxLayout(pseudocodeGroup);
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

    callGraphPanel_ = new CallGraphPanel(analysisTabs);
    callGraphView_ = callGraphPanel_->graphView();

    analysisTabs->addTab(pseudocodeGroup, tr("Pseudocode"));
    analysisTabs->addTab(callGraphPanel_, tr("Call Graph"));

    auto* assemblyGroup = new QGroupBox(tr("Assembly / Opcodes"), detailSplitter);
    auto* assemblyLayout = new QVBoxLayout(assemblyGroup);
    assemblyTable_ = new QTableWidget(assemblyGroup);
    assemblyTable_->setObjectName(QStringLiteral("assemblyTable"));
    assemblyTable_->setColumnCount(4);
    assemblyTable_->setHorizontalHeaderLabels(
        {tr("Address"), tr("Bytes / Opcode"), tr("Mnemonic"), tr("Operand")});
    assemblyTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    assemblyTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    assemblyTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    assemblyTable_->setAlternatingRowColors(true);
    assemblyTable_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    assemblyTable_->verticalHeader()->setVisible(false);
    assemblyTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    assemblyTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    assemblyTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    assemblyTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    assemblyLayout->addWidget(assemblyTable_);

    connect(analysisTabs, &QTabWidget::currentChanged, this, [assemblyGroup, detailSplitter](int index) {
        const bool callGraphIsActive = index == 1;
        assemblyGroup->setVisible(!callGraphIsActive);
        if(!callGraphIsActive) {
            detailSplitter->setSizes({320, 320});
        }
    });

    detailSplitter->addWidget(analysisTabs);
    detailSplitter->addWidget(assemblyGroup);
    detailSplitter->setStretchFactor(0, 1);
    detailSplitter->setStretchFactor(1, 1);
    detailSplitter->setSizes({320, 320});

    analysisSplitter->addWidget(functionPanel);
    analysisSplitter->addWidget(detailSplitter);
    analysisSplitter->setStretchFactor(0, 1);
    analysisSplitter->setStretchFactor(1, 4);
    analysisSplitter->setSizes({260, 900});
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
        [this](QListWidgetItem* current, QListWidgetItem*) { displayFunction(current); });
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
    pseudocodeView_->setCallTargets({});
    pseudocodeView_->clear();
    callGraphPanel_->clearGraph();
    assemblyTable_->clearContents();
    assemblyTable_->setRowCount(0);
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

    if(functionList_->currentItem() != nullptr && functionList_->currentItem()->isHidden()) {
        functionList_->setCurrentItem(firstVisible);
    }
}

void MainWindow::displayFunction(QListWidgetItem* item) {
    pseudocodeView_->clear();
    assemblyTable_->clearContents();
    assemblyTable_->setRowCount(0);
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
    if(!pseudocodeSearch_->text().isEmpty()) {
        auto cursor = pseudocodeView_->textCursor();
        cursor.movePosition(QTextCursor::Start);
        pseudocodeView_->setTextCursor(cursor);
        findPseudocodeText(false);
    }

    assemblyTable_->setRowCount(static_cast<int>(instructions->size()));
    for(std::size_t index = 0; index < instructions->size(); ++index) {
        const auto& instruction = (*instructions)[index];
        auto* addressItem = new QTableWidgetItem(hexadecimal(instruction.address));
        auto* bytesItem = new QTableWidgetItem(byteString(instruction.bytes));
        auto* mnemonicItem =
            new QTableWidgetItem(QString::fromStdString(instruction.mnemonic));
        auto* operandItem =
            new QTableWidgetItem(QString::fromStdString(instruction.operandText));

        addressItem->setData(addressRole, static_cast<qulonglong>(instruction.address));
        addressItem->setData(instructionKindRole, static_cast<int>(instruction.kind));
        if(instruction.directTarget) {
            addressItem->setData(
                directTargetRole, static_cast<qulonglong>(*instruction.directTarget));
        }

        const auto color = instructionColor(instruction.kind);
        if(color.isValid()) {
            const QBrush brush(color);
            addressItem->setForeground(brush);
            bytesItem->setForeground(brush);
            mnemonicItem->setForeground(brush);
            operandItem->setForeground(brush);
        }

        const auto row = static_cast<int>(index);
        assemblyTable_->setItem(row, 0, addressItem);
        assemblyTable_->setItem(row, 1, bytesItem);
        assemblyTable_->setItem(row, 2, mnemonicItem);
        assemblyTable_->setItem(row, 3, operandItem);
    }

    statusBar()->showMessage(
        tr("%1 at %2 - %3 instructions")
            .arg(QString::fromStdString(function->name))
            .arg(hexadecimal(function->address))
            .arg(static_cast<qulonglong>(instructions->size())));
}

void MainWindow::navigateFromAssembly(int row) {
    const auto* addressItem = assemblyTable_->item(row, 0);
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
        tr("Branch target %1 is outside the current function.").arg(hexadecimal(target)));
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
