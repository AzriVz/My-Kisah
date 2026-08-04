#include "MainWindow.hpp"

#include <QApplication>
#include <QCoreApplication>

#include <filesystem>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);

    QCoreApplication::setApplicationName(QStringLiteral("Decompiler"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    decompiler::MainWindow mainWindow;
    if(argc > 1) {
        mainWindow.loadBinary(std::filesystem::path(argv[1]));
    }
    mainWindow.show();

    return QApplication::exec();
}
