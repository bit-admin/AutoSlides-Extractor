#include <QApplication>
#include <QStyleFactory>
#include <QDir>
#include <QStandardPaths>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Set application properties
    app.setApplicationName("AutoSlides Extractor");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("AutoSlidesExtractor");
    app.setOrganizationDomain("autoslidesextractor.com");

    // Set a modern style
    app.setStyle(QStyleFactory::create("Fusion"));

    // Create main window
    MainWindow window;
    window.show();

    return app.exec();
}