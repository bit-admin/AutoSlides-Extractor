#include "cliinstaller.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QStringList>
#include <QSettings>
#include <QProcess>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

QString readWrapperContents(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QByteArray data = f.read(2048);
    f.close();
    return QString::fromUtf8(data);
}

QString quoteForShell(const QString& path)
{
    QString escaped = path;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QStringLiteral("\"") + escaped + QStringLiteral("\"");
}

#ifdef Q_OS_WIN
QString quoteForBatch(const QString& path)
{
    return QStringLiteral("\"") + path + QStringLiteral("\"");
}

QString normalizedWindowsPathForComparison(QString path)
{
    path = path.trimmed();
    if (path.size() >= 2 &&
        path.front() == QLatin1Char('"') &&
        path.back() == QLatin1Char('"')) {
        path = path.mid(1, path.size() - 2);
    }
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

QString batchCommandTarget(const QString& line)
{
    QString command = line.trimmed();
    if (command.startsWith(QStringLiteral("@echo"), Qt::CaseInsensitive)) {
        return QString();
    }

    if (command.startsWith(QLatin1Char('"'))) {
        const qsizetype closingQuote = command.indexOf(QLatin1Char('"'), 1);
        if (closingQuote > 1) {
            return command.mid(1, closingQuote - 1);
        }
        return QString();
    }

    const qsizetype firstSpace = command.indexOf(QLatin1Char(' '));
    return firstSpace > 0 ? command.left(firstSpace) : command;
}

bool wrapperTargetsCurrentExecutable(const QString& contents, const QString& currentBinary)
{
    const QString normalizedCurrent = normalizedWindowsPathForComparison(currentBinary);
    if (normalizedCurrent.isEmpty()) {
        return false;
    }

    const QStringList lines = contents.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& rawLine : lines) {
        const QString target = batchCommandTarget(rawLine);
        if (target.isEmpty()) {
            continue;
        }
        if (normalizedWindowsPathForComparison(target).compare(normalizedCurrent, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }

    // Existing wrappers are simple text files; keep a tolerant fallback for wrappers
    // written by older versions while still normalizing native separators.
    const QString normalizedContents = QDir::fromNativeSeparators(contents);
    return normalizedContents.contains(normalizedCurrent, Qt::CaseInsensitive);
}
#endif

#if defined(Q_OS_WIN)
QString pathSeparator() { return QStringLiteral(";"); }
#else
QString pathSeparator() { return QStringLiteral(":"); }
#endif

#ifdef Q_OS_WIN
QString localAppDataDir()
{
    QString v = qEnvironmentVariable("LOCALAPPDATA");
    if (!v.isEmpty()) return v;
    return QDir::homePath() + QStringLiteral("/AppData/Local");
}
#endif

} // namespace

QString CliInstaller::commandName()
{
#ifdef Q_OS_WIN
    return QStringLiteral("SlidesExtractor.bat");
#else
    return QStringLiteral("SlidesExtractor");
#endif
}

QString CliInstaller::currentExecutablePath()
{
    return QCoreApplication::applicationFilePath();
}

QString CliInstaller::installDirectory()
{
#ifdef Q_OS_WIN
    return QDir::toNativeSeparators(localAppDataDir() + QStringLiteral("/Programs/SlidesExtractor"));
#else
    return QDir::homePath() + QStringLiteral("/.local/bin");
#endif
}

QString CliInstaller::installLocation()
{
    return QDir(installDirectory()).filePath(commandName());
}

CliInstaller::State CliInstaller::installState()
{
    QString location = installLocation();
    if (!QFileInfo::exists(location)) {
        return State::NotInstalled;
    }
    QString contents = readWrapperContents(location);
    QString currentBinary = currentExecutablePath();
    if (contents.isEmpty() || currentBinary.isEmpty()) {
        return State::Stale;
    }
#ifdef Q_OS_WIN
    if (wrapperTargetsCurrentExecutable(contents, currentBinary)) {
        return State::Installed;
    }
#else
    Qt::CaseSensitivity cs = Qt::CaseSensitive;
    if (contents.contains(currentBinary, cs)) {
        return State::Installed;
    }
#endif
    return State::Stale;
}

namespace {

bool pathContainsDir(const QString& pathEnv, const QString& dir, Qt::CaseSensitivity cs)
{
    if (pathEnv.isEmpty()) return false;
    const QStringList parts = pathEnv.split(pathSeparator(), Qt::SkipEmptyParts);
    const QString target = QDir::cleanPath(dir);
    for (const QString& part : parts) {
        if (QDir::cleanPath(part).compare(target, cs) == 0) return true;
    }
    return false;
}

#ifndef Q_OS_WIN
// Probe the user's login+interactive shell for the PATH a fresh terminal would see.
// Required because on macOS, GUI apps launched from Finder inherit a minimal PATH from
// launchd that does not include user dotfile additions like ~/.local/bin.
//
// Cached after first probe — login-shell startup can take 100ms+ and the answer doesn't
// change within a session.
QString loginShellPath()
{
    static bool probed = false;
    static QString cached;
    if (probed) return cached;
    probed = true;

    QString shell = qEnvironmentVariable("SHELL");
    if (shell.isEmpty() || !QFileInfo::exists(shell)) {
        shell = QStringLiteral("/bin/sh");
    }

    QProcess proc;
    proc.setStandardInputFile(QProcess::nullDevice());
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    // -l = login shell (sources zprofile/profile), -i = interactive (sources zshrc/bashrc).
    // Many users put PATH exports in their interactive rc, so both flags are needed.
    proc.start(shell, QStringList() << QStringLiteral("-l")
                                    << QStringLiteral("-i")
                                    << QStringLiteral("-c")
                                    << QStringLiteral("printf '%s' \"$PATH\""));
    if (!proc.waitForStarted(2000)) return cached;
    if (!proc.waitForFinished(3000)) {
        proc.kill();
        proc.waitForFinished(500);
        return cached;
    }
    cached = QString::fromLocal8Bit(proc.readAllStandardOutput()).trimmed();
    return cached;
}
#endif

} // namespace

bool CliInstaller::isInstallDirInPath()
{
    QString dir = installDirectory();
#ifdef Q_OS_WIN
    Qt::CaseSensitivity cs = Qt::CaseInsensitive;
    if (pathContainsDir(qEnvironmentVariable("PATH"), dir, cs)) return true;
    // On Windows, PATH changes from install() take effect in new processes. Also peek at
    // HKCU\Environment so the UI reflects reality immediately after install.
    QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Environment"), QSettings::NativeFormat);
    QString userPath = reg.value(QStringLiteral("Path")).toString();
    return pathContainsDir(userPath, dir, cs);
#else
    Qt::CaseSensitivity cs = Qt::CaseSensitive;
    // Process PATH covers terminal-launched apps. On macOS, GUI apps opened via Finder
    // get a minimal launchd PATH, so this misses user dotfile additions — fall through
    // to the login-shell probe to detect what a fresh terminal would actually see.
    if (pathContainsDir(qEnvironmentVariable("PATH"), dir, cs)) return true;
    return pathContainsDir(loginShellPath(), dir, cs);
#endif
}

QString CliInstaller::pathHint()
{
#ifdef Q_OS_WIN
    return QStringLiteral(
        "The install directory was added to your user PATH. Open a new terminal "
        "for the change to take effect (existing terminals will not see it).");
#else
    return QStringLiteral(
        "Add the following line to your shell rc (~/.zshrc or ~/.bashrc), then open a new terminal:\n")
        + pathExportLine();
#endif
}

QString CliInstaller::pathExportLine()
{
#ifdef Q_OS_WIN
    return QString();
#else
    // Use $HOME so the line is portable across users, since installDirectory() expands
    // QDir::homePath() to an absolute path that wouldn't be reusable by anyone else.
    return QStringLiteral("export PATH=\"$HOME/.local/bin:$PATH\"");
#endif
}

bool CliInstaller::install(QString* errorOut)
{
    QString binary = currentExecutablePath();
    if (binary.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("cannot determine current executable path");
        return false;
    }

    QString dir = installDirectory();
    if (!QDir().mkpath(dir)) {
        if (errorOut) *errorOut = QStringLiteral("cannot create install directory: ") + dir;
        return false;
    }

    QString location = installLocation();

    // If a stale or existing wrapper is in place, overwrite it.
    if (QFile::exists(location)) {
        QFile::remove(location);
    }

    QFile out(location);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorOut) *errorOut = QStringLiteral("cannot write wrapper at: ") + location;
        return false;
    }

    QTextStream ts(&out);
#ifdef Q_OS_WIN
    ts << "@echo off\r\n";
    ts << quoteForBatch(QDir::toNativeSeparators(binary)) << " --cli %*\r\n";
    out.close();
#elif defined(Q_OS_LINUX)
    ts << "#!/bin/sh\n";
    ts << ": \"${QT_QPA_PLATFORM:=offscreen}\"\n";
    ts << "export QT_QPA_PLATFORM\n";
    ts << "exec " << quoteForShell(binary) << " --cli \"$@\"\n";
    out.close();
    QFile::setPermissions(location,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
        QFileDevice::ReadGroup | QFileDevice::ExeGroup |
        QFileDevice::ReadOther | QFileDevice::ExeOther);
#else
    ts << "#!/bin/sh\n";
    ts << "exec " << quoteForShell(binary) << " --cli \"$@\"\n";
    out.close();
    QFile::setPermissions(location,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
        QFileDevice::ReadGroup | QFileDevice::ExeGroup |
        QFileDevice::ReadOther | QFileDevice::ExeOther);
#endif

#ifdef Q_OS_WIN
    // Add install directory to user PATH (HKCU\Environment) if not already present.
    {
        QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Environment"), QSettings::NativeFormat);
        QString currentPath = reg.value(QStringLiteral("Path")).toString();
        QStringList entries = currentPath.split(QLatin1Char(';'), Qt::SkipEmptyParts);
        bool present = false;
        for (const QString& e : entries) {
            if (QDir::cleanPath(e).compare(QDir::cleanPath(dir), Qt::CaseInsensitive) == 0) {
                present = true;
                break;
            }
        }
        if (!present) {
            QString updated = currentPath;
            if (!updated.isEmpty() && !updated.endsWith(QLatin1Char(';'))) {
                updated += QLatin1Char(';');
            }
            updated += QDir::toNativeSeparators(dir);
            reg.setValue(QStringLiteral("Path"), updated);
            reg.sync();

            // Notify other processes that the environment changed.
            DWORD_PTR result = 0;
            SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                                reinterpret_cast<LPARAM>(L"Environment"),
                                SMTO_ABORTIFHUNG, 5000, &result);
        }
    }
#endif

    return true;
}

bool CliInstaller::uninstall(QString* errorOut)
{
    QString location = installLocation();
    if (!QFile::exists(location)) {
        return true;
    }
    if (!QFile::remove(location)) {
        if (errorOut) *errorOut = QStringLiteral("cannot remove wrapper at: ") + location;
        return false;
    }
    return true;
}
