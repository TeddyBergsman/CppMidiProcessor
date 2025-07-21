#include "mainwindow.h"
#include <QApplication>
#include <QMessageBox>
#include "PresetLoader.h"
#include "PresetData.h"
// No longer need <QDir>

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);

    // --- Path to find the resource inside the executable ---
    // The ":/" prefix tells QFile to look inside the embedded resources
    // that were compiled from resources.qrc.
    QString presetPath = ":/preset.xml";

    // Load the preset from the embedded resource.
    PresetLoader loader;
    Preset preset = loader.loadPreset(presetPath);

    // If the preset is not valid, show an error and exit.
    if (!preset.isValid) {
        QMessageBox::critical(nullptr, "Fatal Error", "Could not load or parse the embedded preset.xml resource. The application cannot start.");
        return 1;
    }

    // Initialize the main components with the loaded preset data.
    MainWindow w(preset);
    
    w.show();

    return a.exec();
}