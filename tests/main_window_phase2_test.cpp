#include "ElfFixture.hpp"
#include "ElfLoader.hpp"
#include "MainWindow.hpp"

#include <QApplication>
#include <QLabel>
#include <QString>

#include <iostream>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    test_support::TemporaryElfFiles files;

    const auto validPath = files.write("phase2-valid.elf", test_support::makeElf64Image());
    decompiler::MainWindow mainWindow;
    if(!mainWindow.loadBinary(validPath) || !mainWindow.elfLoader().isValid()) {
        std::cerr << "Valid ELF could not be loaded by MainWindow\n";
        return 1;
    }

    const auto* fileName = mainWindow.findChild<QLabel*>(QStringLiteral("fileNameValue"));
    const auto* elfClass = mainWindow.findChild<QLabel*>(QStringLiteral("elfClassValue"));
    const auto* architecture =
        mainWindow.findChild<QLabel*>(QStringLiteral("architectureValue"));
    if(fileName == nullptr || fileName->text() != QStringLiteral("phase2-valid.elf")
       || elfClass == nullptr || elfClass->text() != QStringLiteral("ELF64")
       || architecture == nullptr || architecture->text() != QStringLiteral("x86-64")) {
        std::cerr << "Binary information was not rendered correctly\n";
        return 1;
    }

    auto invalidImage = test_support::makeElf64Image();
    invalidImage[EI_MAG0] = 0;
    const auto invalidPath = files.write("invalid", invalidImage);
    if(mainWindow.loadBinary(invalidPath) || mainWindow.elfLoader().isValid()) {
        std::cerr << "Invalid ELF was accepted by MainWindow\n";
        return 1;
    }

    if(fileName->text() != QStringLiteral("Not loaded")) {
        std::cerr << "Previous binary information was not cleared after failure\n";
        return 1;
    }

    return 0;
}

