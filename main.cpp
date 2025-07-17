#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[]) {
    // Create the Qt Application instance
    QApplication a(argc, argv);

    // Create and show our main window
    MainWindow w;
    w.show();

    // Start the application's event loop
    return a.exec();
}