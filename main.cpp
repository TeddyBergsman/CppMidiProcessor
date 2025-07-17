#include "mainwindow.h"
#include <QApplication>
#include <QMessageBox>
#include "PresetLoader.h"
#include "PresetData.h"

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);

    // Load the preset from the XML file.
    PresetLoader loader;
    Preset preset = loader.loadPreset("preset.xml");

    // If the preset is not valid, show an error and exit.
    if (!preset.isValid) {
        QMessageBox::critical(nullptr, "Fatal Error", "Could not load or parse preset.xml. The application cannot start.");
        return 1;
    }

    // Initialize the main components with the loaded preset data.
    // The MidiProcessor is now owned by the MainWindow.
    MainWindow w(preset);
    
    w.show();

    return a.exec();
}