#include <QApplication>
#include <QStyleFactory>
#include <QDir>
#include <QStandardPaths>
#include <QStringList>
#include "mainwindow.h"
#include "clirunner.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Set application properties
    app.setApplicationName("AutoSlides Extractor");
    app.setApplicationVersion("1.2.0");
    app.setOrganizationName("AutoSlidesExtractor");
    app.setOrganizationDomain("autoslidesextractor.com");

    // Set a modern style
    app.setStyle(QStyleFactory::create("Fusion"));

    // Use Qt-decoded arguments so Unicode (e.g. Chinese) paths survive on Windows,
    // where argv[] comes through the active code page. On Windows Qt internally
    // uses GetCommandLineW(); on Linux/macOS the system locale is UTF-8.
    QStringList allArgs = QCoreApplication::arguments();
    bool cliMode = false;
    QStringList cliArgs;
    cliArgs.reserve(allArgs.size());
    for (int i = 0; i < allArgs.size(); ++i) {
        const QString& a = allArgs.at(i);
        if (i > 0 && a == QStringLiteral("--cli")) {
            cliMode = true;
            continue;
        }
        cliArgs << a;
    }

    if (cliMode) {
        CliRunner runner;
        return runner.run(cliArgs);
    }

    // Create main window
    MainWindow window;
    window.show();

    return app.exec();
}
