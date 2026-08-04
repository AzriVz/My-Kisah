#pragma once

#include <QMainWindow>

#include <cstdint>
#include <filesystem>
#include <memory>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QString;
class QTableWidget;

namespace decompiler {

class AnalysisSession;
class ElfLoader;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    bool loadBinary(const std::filesystem::path& path);
    [[nodiscard]] const ElfLoader& elfLoader() const noexcept;
    [[nodiscard]] const AnalysisSession& analysisSession() const noexcept;

private:
    void chooseBinary();
    void clearBinaryInformation();
    void clearAnalysisViews();
    void updateBinaryInformation();
    void populateFunctionList();
    void filterFunctions(const QString& query);
    void displayFunction(QListWidgetItem* item);
    void navigateFromAssembly(int row);
    bool selectFunction(std::uint64_t address);

    std::unique_ptr<AnalysisSession> analysisSession_;
    QLabel* fileNameValue_ = nullptr;
    QLabel* filePathValue_ = nullptr;
    QLabel* fileSizeValue_ = nullptr;
    QLabel* classValue_ = nullptr;
    QLabel* architectureValue_ = nullptr;
    QLabel* endiannessValue_ = nullptr;
    QLabel* entryPointValue_ = nullptr;
    QLabel* textAddressValue_ = nullptr;
    QLabel* textSizeValue_ = nullptr;
    QLabel* sectionCountValue_ = nullptr;
    QLabel* symbolCountValue_ = nullptr;
    QLabel* functionCountValue_ = nullptr;
    QLabel* strippedValue_ = nullptr;
    QLineEdit* functionSearch_ = nullptr;
    QListWidget* functionList_ = nullptr;
    QPlainTextEdit* pseudocodeView_ = nullptr;
    QTableWidget* assemblyTable_ = nullptr;
};

} // namespace decompiler
