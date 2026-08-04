#include "MainWindow.hpp"

#include <QApplication>
#include <QCoreApplication>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);

    QCoreApplication::setApplicationName(QStringLiteral("Decompiler"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    decompiler::MainWindow mainWindow;
    mainWindow.show();

    return QApplication::exec();
}
