#include "MainWindow.hpp"

#include <QStatusBar>

namespace decompiler {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle(tr("Decompiler"));
    resize(1100, 700);
    statusBar()->showMessage(tr("Ready"));
}

} // namespace decompiler
