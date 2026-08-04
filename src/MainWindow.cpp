#include "MainWindow.hpp"

#include "AnalysisSession.hpp"
#include "ElfLoader.hpp"
#include "FunctionInfo.hpp"
#include "Instruction.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QBrush>
#include <QColor>
#include <QFileDialog>
#include <QFontDatabase>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QStatusBar>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cstdint>
#include <string>

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

    auto* pseudocodeGroup = new QGroupBox(tr("Reconstructed Pseudocode"), detailSplitter);
    auto* pseudocodeLayout = new QVBoxLayout(pseudocodeGroup);
    pseudocodeView_ = new QPlainTextEdit(pseudocodeGroup);
    pseudocodeView_->setObjectName(QStringLiteral("pseudocodeView"));
    pseudocodeView_->setReadOnly(true);
    pseudocodeView_->setLineWrapMode(QPlainTextEdit::NoWrap);
    pseudocodeView_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    pseudocodeView_->setPlaceholderText(tr("Open a binary to reconstruct pseudocode."));
    pseudocodeLayout->addWidget(pseudocodeView_);

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

    detailSplitter->addWidget(pseudocodeGroup);
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

    clearBinaryInformation();
    clearAnalysisViews();
    statusBar()->showMessage(tr("Ready"));
}

MainWindow::~MainWindow() = default;

bool MainWindow::loadBinary(const std::filesystem::path& path) {
    clearAnalysisViews();
    if(!analysisSession_->analyze(path)) {
        clearBinaryInformation();
        setWindowTitle(tr("Decompiler"));
        statusBar()->showMessage(
            tr("Failed to analyze binary: %1")
                .arg(QString::fromStdString(std::string(analysisSession_->errorMessage()))));
        return false;
    }

    updateBinaryInformation();
    populateFunctionList();

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
    functionList_->clear();
    pseudocodeView_->clear();
    assemblyTable_->clearContents();
    assemblyTable_->setRowCount(0);
}

void MainWindow::updateBinaryInformation() {
    const auto& loader = analysisSession_->elfLoader();
    const auto& metadata = loader.metadata();
    const auto textSection = loader.findSection(".text");

    fileNameValue_->setText(QString::fromStdString(metadata.fileName));
    filePathValue_->setText(QString::fromStdString(metadata.filePath.string()));
    fileSizeValue_->setText(
        tr("%1 bytes").arg(static_cast<qulonglong>(metadata.fileSize)));
    classValue_->setText(metadata.is64Bit ? tr("ELF64") : tr("Unsupported"));
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

    pseudocodeView_->setPlainText(QString::fromStdString(*pseudocode));

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
