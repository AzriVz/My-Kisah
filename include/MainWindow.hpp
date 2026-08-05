#pragma once

#include <QMainWindow>

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

class QAction;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QProgressBar;
class QString;
namespace decompiler {

class AnalysisSession;
class AssemblyGraphTable;
class CallGraphPanel;
class CallGraphView;
class ElfLoader;
class PseudocodeView;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    bool loadBinary(const std::filesystem::path& path);
    bool patchInstruction(
        std::uint64_t instructionAddress,
        std::span<const std::uint8_t> replacementBytes,
        const std::filesystem::path& outputPath,
        bool allowOverwrite = false);
    [[nodiscard]] const ElfLoader& elfLoader() const noexcept;
    [[nodiscard]] const AnalysisSession& analysisSession() const noexcept;

private:
    void chooseBinary();
    void patchSelectedInstruction();
    void clearBinaryInformation();
    void clearAnalysisViews();
    void updateBinaryInformation();
    void populateFunctionList();
    void populateAssemblyListing();
    void focusAssemblyFunction(std::uint64_t address);
    void filterFunctions(const QString& query);
    void displayFunction(QListWidgetItem* item);
    void navigateFromAssembly(int row);
    void navigateFromPseudocode(std::uint64_t address);
    void navigateBack();
    void navigateForward();
    void recordNavigation(std::uint64_t address);
    void resetNavigation();
    void updateNavigationActions();
    void updatePseudocodeCallTargets();
    void findPseudocodeText(bool backward);
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
    QLineEdit* pseudocodeSearch_ = nullptr;
    QListWidget* functionList_ = nullptr;
    PseudocodeView* pseudocodeView_ = nullptr;
    CallGraphPanel* callGraphPanel_ = nullptr;
    CallGraphView* callGraphView_ = nullptr;
    AssemblyGraphTable* assemblyTable_ = nullptr;
    QProgressBar* analysisProgress_ = nullptr;
    QAction* backAction_ = nullptr;
    QAction* forwardAction_ = nullptr;
    QAction* patchInstructionAction_ = nullptr;
    std::unordered_map<std::uint64_t, int> assemblyFunctionRows_;
    std::unordered_map<std::uint64_t, std::size_t> assemblyFunctionInstructionCounts_;
    std::vector<std::uint64_t> navigationHistory_;
    std::ptrdiff_t navigationIndex_ = -1;
    bool restoringNavigation_ = false;
    bool preserveAssemblyEntryOnNextSelection_ = false;
    bool suppressAssemblyFocus_ = false;
};

} // namespace decompiler
