#include "MainWindow.hpp"

#include "ElfLoader.hpp"

#include <elf.h>

#include <QAction>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

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

namespace decompiler {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , elfLoader_(std::make_unique<ElfLoader>()) {
    setWindowTitle(tr("Decompiler"));
    resize(1100, 700);

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

    auto* introduction = new QLabel(
        tr("Open a supported ELF binary to inspect its metadata."), centralWidget);
    introduction->setWordWrap(true);
    pageLayout->addWidget(introduction);

    auto* informationGroup = new QGroupBox(tr("Binary Information"), centralWidget);
    auto* formLayout = new QFormLayout(informationGroup);
    formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

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
    functionSymbolCountValue_ = createValueLabel(informationGroup, "functionSymbolCountValue");
    strippedValue_ = createValueLabel(informationGroup, "strippedValue");

    formLayout->addRow(tr("File name:"), fileNameValue_);
    formLayout->addRow(tr("File path:"), filePathValue_);
    formLayout->addRow(tr("File size:"), fileSizeValue_);
    formLayout->addRow(tr("ELF class:"), classValue_);
    formLayout->addRow(tr("Architecture:"), architectureValue_);
    formLayout->addRow(tr("Endianness:"), endiannessValue_);
    formLayout->addRow(tr("Entry point:"), entryPointValue_);
    formLayout->addRow(tr(".text address:"), textAddressValue_);
    formLayout->addRow(tr(".text size:"), textSizeValue_);
    formLayout->addRow(tr("Sections:"), sectionCountValue_);
    formLayout->addRow(tr("Symbols:"), symbolCountValue_);
    formLayout->addRow(tr("Function symbols:"), functionSymbolCountValue_);
    formLayout->addRow(tr("Stripped:"), strippedValue_);

    pageLayout->addWidget(informationGroup);
    pageLayout->addStretch(1);
    setCentralWidget(centralWidget);

    clearBinaryInformation();
    statusBar()->showMessage(tr("Ready"));
}

MainWindow::~MainWindow() = default;

bool MainWindow::loadBinary(const std::filesystem::path& path) {
    if(!elfLoader_->load(path)) {
        clearBinaryInformation();
        setWindowTitle(tr("Decompiler"));
        statusBar()->showMessage(
            tr("Failed to open binary: %1")
                .arg(QString::fromStdString(std::string(elfLoader_->errorMessage()))));
        return false;
    }

    updateBinaryInformation();
    const auto fileName = QString::fromStdString(elfLoader_->metadata().fileName);
    setWindowTitle(tr("Decompiler - %1").arg(fileName));
    statusBar()->showMessage(tr("Loaded %1").arg(fileName));
    return true;
}

const ElfLoader& MainWindow::elfLoader() const noexcept {
    return *elfLoader_;
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
            QString::fromStdString(std::string(elfLoader_->errorMessage())));
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
        functionSymbolCountValue_,
        strippedValue_,
    };

    for(auto* label : labels) {
        label->setText(emptyValue);
    }
}

void MainWindow::updateBinaryInformation() {
    const auto& metadata = elfLoader_->metadata();
    const auto textSection = elfLoader_->findSection(".text");
    const auto functionSymbolCount = std::count_if(
        elfLoader_->symbols().begin(),
        elfLoader_->symbols().end(),
        [](const SymbolInfo& symbol) { return symbol.type == STT_FUNC; });

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
    functionSymbolCountValue_->setText(QString::number(functionSymbolCount));
    strippedValue_->setText(metadata.isStripped ? tr("Yes") : tr("No"));
}

} // namespace decompiler
