#pragma once

#include <QMainWindow>

namespace decompiler {

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
};

} // namespace decompiler

