#pragma once

#include <QMainWindow>

#include <filesystem>
#include <memory>

class QLabel;

namespace decompiler {

class ElfLoader;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    bool loadBinary(const std::filesystem::path& path);
    [[nodiscard]] const ElfLoader& elfLoader() const noexcept;

private:
    void chooseBinary();
    void clearBinaryInformation();
    void updateBinaryInformation();

    std::unique_ptr<ElfLoader> elfLoader_;
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
    QLabel* functionSymbolCountValue_ = nullptr;
    QLabel* strippedValue_ = nullptr;
};

} // namespace decompiler
