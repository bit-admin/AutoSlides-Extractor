#include <QCoreApplication>
#include <QStringList>
#include "clirunner.h"

#ifndef AUTOSLIDES_ELECTRON_HELPER
#include <QApplication>
#include <QStyleFactory>
#ifdef Q_OS_LINUX
#include <QFileInfo>
#include <QIcon>
#endif
#include "mainwindow.h"
#endif

namespace {

void setApplicationProperties()
{
    QCoreApplication::setApplicationName(QStringLiteral("AutoSlides Extractor"));
#ifdef AUTOSLIDES_ELECTRON_HELPER
    QCoreApplication::setApplicationVersion(QStringLiteral(AUTOSLIDES_HELPER_VERSION));
#else
    QCoreApplication::setApplicationVersion(QStringLiteral("2.2.0"));
#endif
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

#ifndef AUTOSLIDES_ELECTRON_HELPER
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
#endif

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

int runCli(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    setApplicationProperties();
    CliRunner runner;
    return runner.run(cliArguments());
}

} // namespace

int main(int argc, char *argv[])
{
#ifdef AUTOSLIDES_ELECTRON_HELPER
    return runCli(argc, argv);
#else
    if (hasUserArguments(argc, argv)) {
        return runCli(argc, argv);
    }

    QApplication app(argc, argv);
    setApplicationProperties();

#ifdef Q_OS_LINUX
    // Windows/macOS take the icon from the .rc/.icns. On Linux, match the installed
    // .desktop entry (Wayland app_id) and load the icon installed next to the binary
    // (AppImage / `cmake --install` layout), falling back to the icon theme.
    QGuiApplication::setDesktopFileName(QStringLiteral("AutoSlidesExtractor"));
    const QString bundledIcon = QCoreApplication::applicationDirPath()
        + QStringLiteral("/../share/icons/hicolor/256x256/apps/AutoSlidesExtractor.png");
    app.setWindowIcon(QFileInfo::exists(bundledIcon)
                          ? QIcon(bundledIcon)
                          : QIcon::fromTheme(QStringLiteral("AutoSlidesExtractor")));
#endif

    // Set a modern style
    app.setStyle(QStyleFactory::create("Fusion"));

    // Create main window
    MainWindow window;
    window.show();

    return app.exec();
#endif
}
