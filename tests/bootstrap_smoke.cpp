#include "MainWindow.hpp"

#include <capstone/capstone.h>

#include <QApplication>
#include <QCoreApplication>
#include <QString>

#include <iostream>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);

    int capstoneMajor = 0;
    int capstoneMinor = 0;
    if(cs_version(&capstoneMajor, &capstoneMinor) <= 0 || capstoneMajor <= 0) {
        std::cerr << "Capstone version could not be queried\n";
        return 1;
    }

    decompiler::MainWindow mainWindow;
    if(mainWindow.windowTitle() != QStringLiteral("Decompiler")) {
        std::cerr << "Unexpected main-window title\n";
        return 1;
    }

    mainWindow.show();
    QCoreApplication::processEvents();

    if(!mainWindow.isVisible()) {
        std::cerr << "Main window could not be shown\n";
        return 1;
    }

    return 0;
}
