#ifndef CLIINSTALLER_H
#define CLIINSTALLER_H

#include <QString>

class CliInstaller
{
public:
    enum class State {
        NotInstalled,
        Installed,
        Stale  // wrapper exists but points at a different binary
    };

    static QString commandName();
    static QString currentExecutablePath();
    static QString installLocation();
    static QString installDirectory();
    static State   installState();
    static bool    isInstallDirInPath();
    static QString pathHint();
    static QString pathExportLine();   // shell line to add the install dir to PATH; empty on Windows
    static bool    install(QString* errorOut);
    static bool    uninstall(QString* errorOut);
};

#endif // CLIINSTALLER_H
