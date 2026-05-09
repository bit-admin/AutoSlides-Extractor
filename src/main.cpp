#include <QApplication>
#include <QCoreApplication>
#include <QStyleFactory>
#include <QStringList>
#include "mainwindow.h"
#include "clirunner.h"

namespace {

void setApplicationProperties()
{
    QCoreApplication::setApplicationName(QStringLiteral("AutoSlides Extractor"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.2.1"));
    QCoreApplication::setOrganizationName(QStringLiteral("AutoSlidesExtractor"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("autoslidesextractor.com"));
}

bool isMacProcessSerialArgument(const QString& argument)
{
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    return argument.startsWith(QStringLiteral("-psn_"));
#else
    Q_UNUSED(argument);
    return false;
#endif
}

bool hasUserArguments(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        const QString argument = QString::fromLocal8Bit(argv[i]);
        if (isMacProcessSerialArgument(argument)) {
            continue;
        }
        return true;
    }
    return false;
}

QStringList cliArguments()
{
    // Use Qt-decoded arguments so Unicode (e.g. Chinese) paths survive on Windows,
    // where argv[] comes through the active code page. On Windows Qt internally
    // uses GetCommandLineW(); on Linux/macOS the system locale is UTF-8.
    const QStringList allArgs = QCoreApplication::arguments();
    QStringList args;
    args.reserve(allArgs.size());
    for (int i = 0; i < allArgs.size(); ++i) {
        const QString& argument = allArgs.at(i);
        if (i > 0 && (argument == QStringLiteral("--cli") || isMacProcessSerialArgument(argument))) {
            continue;
        }
        args << argument;
    }
    return args;
}

} // namespace

int main(int argc, char *argv[])
{
    if (hasUserArguments(argc, argv)) {
        QCoreApplication app(argc, argv);
        setApplicationProperties();

        CliRunner runner;
        return runner.run(cliArguments());
    }

    QApplication app(argc, argv);
    setApplicationProperties();

    // Set a modern style
    app.setStyle(QStyleFactory::create("Fusion"));

    // Create main window
    MainWindow window;
    window.show();

    return app.exec();
}
