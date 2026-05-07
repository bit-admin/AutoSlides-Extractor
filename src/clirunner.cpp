#include "clirunner.h"
#include "processingthread.h"
#include "videoqueue.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QEventLoop>
#include <QTextStream>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QMetaObject>
#include <atomic>
#include <csignal>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

namespace {

constexpr int EXIT_OK = 0;
constexpr int EXIT_BAD_ARGS = 2;
constexpr int EXIT_BAD_INPUT = 3;
constexpr int EXIT_PROCESSING_FAILED = 4;
constexpr int EXIT_POSTPROCESSING_FAILED = 5;
constexpr int EXIT_CANCELLED_SIGINT = 130;
constexpr int EXIT_CANCELLED_SIGTERM = 143;

// Shared with signal handlers. Set by the OS-level signal handler; consumed
// on the Qt thread via a posted metacall to CliRunner::requestCancel().
std::atomic<bool> g_cancelRequested{false};
std::atomic<int> g_cancelSignal{0}; // SIGTERM or SIGINT
CliRunner* g_activeRunner = nullptr;

bool parseBoolFlag(const QString& value, bool* out)
{
    QString v = value.trimmed().toLower();
    if (v == QLatin1String("true") || v == QLatin1String("1") || v == QLatin1String("yes") || v == QLatin1String("on")) {
        *out = true;
        return true;
    }
    if (v == QLatin1String("false") || v == QLatin1String("0") || v == QLatin1String("no") || v == QLatin1String("off")) {
        *out = false;
        return true;
    }
    return false;
}

} // namespace

CliRunner::CliRunner(QObject* parent)
    : QObject(parent),
      m_phashRedundant(false),
      m_phashExclusion(false),
      m_mlClassify(false),
      m_exclusionListOverridden(false),
      m_processingResultCode(EXIT_OK),
      m_processingSlideCount(0),
      m_processingFinished(false),
      m_lastFramePercent(-100.0),
      m_lastPostCurrent(-1),
      m_progressBarActive(false),
      m_jsonMode(false),
      m_postProcessingFailed(false),
      m_postStage(QStringLiteral("phash")),
      m_cancelHandled(false),
      m_activeThread(nullptr)
{
}

void CliRunner::attachWindowsConsole()
{
#ifdef _WIN32
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* fp = nullptr;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        freopen_s(&fp, "CONIN$",  "r", stdin);
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        std::setvbuf(stderr, nullptr, _IONBF, 0);
    }
    // Render UTF-8 bytes (paths, [info] lines) correctly in the host console
    // regardless of the system's active code page (e.g. CP936, CP1252).
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

void CliRunner::writeStdout(const QString& line)
{
    QTextStream out(stdout);
    out.setEncoding(QStringConverter::Utf8);
    if (m_progressBarActive) {
        out << '\n';
        m_progressBarActive = false;
    }
    out << line << '\n';
    out.flush();
}

void CliRunner::writeStderr(const QString& line)
{
    if (m_progressBarActive) {
        QTextStream out(stdout);
        out.setEncoding(QStringConverter::Utf8);
        out << '\n';
        out.flush();
        m_progressBarActive = false;
    }
    QTextStream err(stderr);
    err.setEncoding(QStringConverter::Utf8);
    err << line << '\n';
    err.flush();
}

void CliRunner::writeProgressBar(double percentage)
{
    constexpr int kBarWidth = 30;
    if (percentage < 0.0) percentage = 0.0;
    if (percentage > 100.0) percentage = 100.0;
    int filled = static_cast<int>(percentage / 100.0 * kBarWidth + 0.5);
    if (filled < 0) filled = 0;
    if (filled > kBarWidth) filled = kBarWidth;
    QString bar(filled, QLatin1Char('#'));
    bar.append(QString(kBarWidth - filled, QLatin1Char('-')));
    QTextStream out(stdout);
    out.setEncoding(QStringConverter::Utf8);
    out << '\r' << QString(QStringLiteral("[frame] [%1] %2%")).arg(bar).arg(percentage, 5, 'f', 1);
    out.flush();
    m_progressBarActive = true;
}

void CliRunner::finishProgressBar()
{
    if (!m_progressBarActive) return;
    QTextStream out(stdout);
    out.setEncoding(QStringConverter::Utf8);
    out << '\n';
    out.flush();
    m_progressBarActive = false;
}

void CliRunner::emitEvent(const QString& event, QJsonObject fields, bool toStderr)
{
    fields.insert(QStringLiteral("v"), 1);
    fields.insert(QStringLiteral("event"), event);
    fields.insert(QStringLiteral("ts"),
                  QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    const QByteArray line = QJsonDocument(fields).toJson(QJsonDocument::Compact);
    FILE* fp = toStderr ? stderr : stdout;
    std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), fp);
    std::fputc('\n', fp);
    std::fflush(fp);
}

void CliRunner::emitError(const QString& category, const QString& message, int exitCode)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("category"), category);
    obj.insert(QStringLiteral("message"), message);
    obj.insert(QStringLiteral("exitCode"), exitCode);
    emitEvent(QStringLiteral("error"), obj, /*toStderr=*/true);
}

QJsonObject CliRunner::parseVideoInfoString(const QString& raw) const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("kind"), QStringLiteral("video"));
    obj.insert(QStringLiteral("message"), raw);

    // Format from processingthread.cpp:207-215:
    //   "Video Info - Resolution: WxH, Duration: Ds, Frame Rate: Rfps,
    //    I-Frame Interval: Is, Screen Recording: Yes|No, Decoder: NAME"
    static const QRegularExpression re(QStringLiteral(
        "Resolution:\\s*(\\d+)x(\\d+),\\s*"
        "Duration:\\s*([0-9.]+)s,\\s*"
        "Frame Rate:\\s*([0-9.]+)fps,\\s*"
        "I-Frame Interval:\\s*([0-9.]+)s,\\s*"
        "Screen Recording:\\s*(Yes|No),\\s*"
        "Decoder:\\s*(.+)$"));
    const auto m = re.match(raw);
    if (m.hasMatch()) {
        obj.insert(QStringLiteral("videoWidth"), m.captured(1).toInt());
        obj.insert(QStringLiteral("videoHeight"), m.captured(2).toInt());
        obj.insert(QStringLiteral("durationSec"), m.captured(3).toDouble());
        obj.insert(QStringLiteral("frameRate"), m.captured(4).toDouble());
        obj.insert(QStringLiteral("iFrameIntervalSec"), m.captured(5).toDouble());
        obj.insert(QStringLiteral("screenRecording"), m.captured(6) == QStringLiteral("Yes"));
        obj.insert(QStringLiteral("decoder"), m.captured(7).trimmed());
    }
    return obj;
}

QString CliRunner::trashCategoryForReason(const QString& reason) const
{
    if (reason.startsWith(QStringLiteral("Duplicate"), Qt::CaseInsensitive)) {
        return QStringLiteral("phash_duplicate");
    }
    if (reason.startsWith(QStringLiteral("Excluded"), Qt::CaseInsensitive)) {
        return QStringLiteral("phash_excluded");
    }
    if (reason.startsWith(QStringLiteral("ML:"), Qt::CaseInsensitive)) {
        // Extract class label: "ML: <class> (confidence: ...)"
        const int colon = reason.indexOf(QLatin1Char(':'));
        const int paren = reason.indexOf(QLatin1Char('('));
        if (colon >= 0) {
            const int end = paren > colon ? paren : reason.size();
            const QString cls = reason.mid(colon + 1, end - colon - 1).trimmed();
            if (cls.startsWith(QStringLiteral("not_slide"))) {
                return QStringLiteral("ml_not_slide");
            }
            if (cls.startsWith(QStringLiteral("may_be_slide"))) {
                return QStringLiteral("ml_maybe_slide");
            }
        }
        return QStringLiteral("ml");
    }
    return QStringLiteral("other");
}

namespace {

extern "C" void cli_signal_handler(int sig)
{
    if (g_cancelRequested.exchange(true)) {
        // Already requested. Second hit: bail out hard.
        std::_Exit(sig == SIGINT ? EXIT_CANCELLED_SIGINT : EXIT_CANCELLED_SIGTERM);
    }
    g_cancelSignal.store(sig);
    if (g_activeRunner) {
        QMetaObject::invokeMethod(g_activeRunner, "requestCancel", Qt::QueuedConnection);
    }
}

#ifdef _WIN32
BOOL WINAPI cli_console_ctrl_handler(DWORD ctrl)
{
    switch (ctrl) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
            cli_signal_handler(SIGINT);
            return TRUE;
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            cli_signal_handler(SIGTERM);
            return TRUE;
        default:
            return FALSE;
    }
}
#endif

} // namespace

void CliRunner::installSignalHandlers()
{
    g_activeRunner = this;
    g_cancelRequested.store(false);
    g_cancelSignal.store(0);
    std::signal(SIGTERM, cli_signal_handler);
    std::signal(SIGINT, cli_signal_handler);
#ifdef _WIN32
    SetConsoleCtrlHandler(cli_console_ctrl_handler, TRUE);
#endif
}

void CliRunner::uninstallSignalHandlers()
{
    std::signal(SIGTERM, SIG_DFL);
    std::signal(SIGINT, SIG_DFL);
#ifdef _WIN32
    SetConsoleCtrlHandler(cli_console_ctrl_handler, FALSE);
#endif
    g_activeRunner = nullptr;
}

void CliRunner::requestCancel()
{
    // Posted from cli_signal_handler via QMetaObject::invokeMethod.
    if (m_activeThread) {
        m_activeThread->stopProcessing();
    } else {
        // No active processing thread — terminate the event loop directly
        // so run() can emit the cancelled event and exit.
        QCoreApplication::quit();
    }
}

int CliRunner::run(const QStringList& arguments)
{
    ConfigManager configManager;
    m_config = configManager.loadConfig();

    // Parse args first so we know whether to operate in JSON mode. parseArgs
    // does not write any progress output; it only emits help/version (which
    // call std::exit) and parse errors via writeStderr/emitError.
    QString parseError;
    if (!parseArgs(arguments, &parseError)) {
        if (!parseError.isEmpty()) {
            if (m_jsonMode) {
                emitError(QStringLiteral("bad_args"), parseError, EXIT_BAD_ARGS);
            } else {
                writeStderr(QStringLiteral("error: ") + parseError);
            }
        }
        return EXIT_BAD_ARGS;
    }

    if (!m_jsonMode) {
        attachWindowsConsole();
    } else {
        // Ensure each NDJSON line is flushed promptly to the parent's pipe,
        // even on Windows where MSVCRT can default to full buffering.
        std::setvbuf(stdout, nullptr, _IOLBF, 4096);
        std::setvbuf(stderr, nullptr, _IONBF, 0);
    }

    installSignalHandlers();

    QFileInfo videoInfo(m_videoPath);
    if (!videoInfo.exists() || !videoInfo.isFile()) {
        const QString msg = QStringLiteral("video file not found: ") + m_videoPath;
        if (m_jsonMode) {
            emitError(QStringLiteral("bad_input"), msg, EXIT_BAD_INPUT);
        } else {
            writeStderr(QStringLiteral("error: ") + msg);
        }
        uninstallSignalHandlers();
        return EXIT_BAD_INPUT;
    }
    m_videoPath = videoInfo.absoluteFilePath();

    QDir outDir(m_outputDir);
    if (!outDir.exists()) {
        if (!QDir().mkpath(m_outputDir)) {
            const QString msg = QStringLiteral("cannot create output directory: ") + m_outputDir;
            if (m_jsonMode) {
                emitError(QStringLiteral("bad_input"), msg, EXIT_BAD_INPUT);
            } else {
                writeStderr(QStringLiteral("error: ") + msg);
            }
            uninstallSignalHandlers();
            return EXIT_BAD_INPUT;
        }
    }
    m_outputDir = outDir.absolutePath();

    m_config.outputDirectory = m_outputDir;
    m_config.enablePostProcessing = (m_phashRedundant || m_phashExclusion || m_mlClassify);
    m_config.deleteRedundant = m_phashRedundant;
    m_config.compareExcluded = m_phashExclusion;
    m_config.enableMLClassification = m_mlClassify;

    if (m_phashExclusion && !m_exclusionListOverridden) {
        // Default: pull the saved GUI exclusion list. CLI-supplied hashes
        // (when --phash-exclusion-hashes was given) are NOT persisted back to GUI config.
        m_exclusionList = configManager.loadExclusionList();
        if (m_exclusionList.isEmpty()) {
            const QString msg =
                QStringLiteral("--phash-exclusion enabled but the saved GUI "
                               "exclusion list is empty; phase 2 will be a no-op.");
            if (m_jsonMode) {
                QJsonObject obj;
                obj.insert(QStringLiteral("kind"), QStringLiteral("warning"));
                obj.insert(QStringLiteral("message"), msg);
                emitEvent(QStringLiteral("info"), obj);
            } else {
                writeStdout(QStringLiteral("warning: ") + msg);
            }
        }
    }

    const double ssim = ConfigManager::getSSIMThreshold(m_config.ssimPreset, m_config.customSSIMThreshold);
    QJsonArray phases;
    if (m_phashRedundant) phases.append(QStringLiteral("phash-redundant"));
    if (m_phashExclusion) phases.append(QStringLiteral("phash-exclusion"));
    if (m_mlClassify)     phases.append(QStringLiteral("ml-classify"));

    if (m_jsonMode) {
        QJsonObject obj;
        obj.insert(QStringLiteral("schemaVersion"), 1);
        obj.insert(QStringLiteral("appVersion"), QCoreApplication::applicationVersion());
        obj.insert(QStringLiteral("video"), m_videoPath);
        obj.insert(QStringLiteral("output"), m_outputDir);
        obj.insert(QStringLiteral("ssimThreshold"), ssim);
        obj.insert(QStringLiteral("phases"), phases);
        obj.insert(QStringLiteral("pid"), static_cast<qint64>(QCoreApplication::applicationPid()));
        emitEvent(QStringLiteral("start"), obj);
    } else {
        writeStdout(QStringLiteral("SlidesExtractor: starting"));
        writeStdout(QStringLiteral("  video:  ") + m_videoPath);
        writeStdout(QStringLiteral("  output: ") + m_outputDir);
        writeStdout(QStringLiteral("  ssim threshold: ") + QString::number(ssim, 'f', 4));
        writeStdout(QStringLiteral("  phases: ") +
                    (m_phashRedundant ? QStringLiteral("phash-redundant ") : QString()) +
                    (m_phashExclusion ? QStringLiteral("phash-exclusion ") : QString()) +
                    (m_mlClassify ? QStringLiteral("ml-classify") : QString()) +
                    (!m_config.enablePostProcessing ? QStringLiteral("(none)") : QString()));
    }

    QString slidesDir;
    int slideCount = 0;
    int processingRc = runProcessingStage(&slidesDir, &slideCount);
    if (processingRc != EXIT_OK) {
        uninstallSignalHandlers();
        return processingRc;
    }

    if (m_config.enablePostProcessing) {
        int postRc = runPostProcessingStage(slidesDir);
        if (postRc != EXIT_OK) {
            uninstallSignalHandlers();
            return postRc;
        }
    }

    if (m_jsonMode) {
        QJsonObject obj;
        obj.insert(QStringLiteral("slideCount"), slideCount);
        obj.insert(QStringLiteral("slidesDir"), slidesDir);
        obj.insert(QStringLiteral("exitCode"), EXIT_OK);
        emitEvent(QStringLiteral("done"), obj);
    } else {
        writeStdout(QString(QStringLiteral("done: %1 slides at %2")).arg(slideCount).arg(slidesDir));
    }
    uninstallSignalHandlers();
    return EXIT_OK;
}

bool CliRunner::parseArgs(const QStringList& arguments, QString* errorOut)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("SlidesExtractor — extract slide images from a video.\n"
                       "Defaults are taken from the AutoSlides Extractor saved configuration.\n"
                       "Pass --video and --output; override anything else explicitly."));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption videoOpt(QStringList() << QStringLiteral("video"),
        QStringLiteral("Path to the input video file (required)."), QStringLiteral("path"));
    QCommandLineOption outputOpt(QStringList() << QStringLiteral("output"),
        QStringLiteral("Output directory for extracted slides (required)."), QStringLiteral("dir"));

    QCommandLineOption ssimOpt(QStringLiteral("ssim-threshold"),
        QStringLiteral("Custom SSIM threshold (e.g. 0.9985). Forces preset=Custom."), QStringLiteral("f"));
    QCommandLineOption downsampleOpt(QStringLiteral("enable-downsampling"),
        QStringLiteral("Enable downsampling: true|false."), QStringLiteral("bool"));
    QCommandLineOption dsWidthOpt(QStringLiteral("downsample-width"),
        QStringLiteral("Downsample width in pixels."), QStringLiteral("w"));
    QCommandLineOption dsHeightOpt(QStringLiteral("downsample-height"),
        QStringLiteral("Downsample height in pixels."), QStringLiteral("h"));
    QCommandLineOption chunkOpt(QStringLiteral("chunk-size"),
        QStringLiteral("Frame chunk size."), QStringLiteral("n"));
    QCommandLineOption jpegOpt(QStringLiteral("jpeg-quality"),
        QStringLiteral("JPEG quality 1..100."), QStringLiteral("n"));

    QCommandLineOption phashRedundantOpt(QStringLiteral("phash-redundant"),
        QStringLiteral("Enable phase 1: remove redundant slides via pHash."));
    QCommandLineOption phashExclusionOpt(QStringLiteral("phash-exclusion"),
        QStringLiteral("Enable phase 2: pHash exclusion list. Defaults to the saved GUI list."));
    QCommandLineOption phashExclusionHashesOpt(QStringLiteral("phash-exclusion-hashes"),
        QStringLiteral("Override the exclusion list with comma-separated 64-char hex hashes. "
                       "Implies --phash-exclusion. Not saved to GUI config."), QStringLiteral("hex,hex,..."));
    QCommandLineOption hammingOpt(QStringLiteral("hamming-threshold"),
        QStringLiteral("Hamming distance threshold (shared by phase 1 and 2)."), QStringLiteral("n"));

    QCommandLineOption mlOpt(QStringLiteral("ml-classify"),
        QStringLiteral("Enable phase 3: ML classification."));
    QCommandLineOption mlModelOpt(QStringLiteral("ml-model"),
        QStringLiteral("Path to ONNX model. Use empty or omit for built-in model."), QStringLiteral("path"));
    QCommandLineOption mlEpOpt(QStringLiteral("ml-execution-provider"),
        QStringLiteral("Execution provider: Auto|CoreML|CUDA|DirectML|CPU."), QStringLiteral("name"));
    QCommandLineOption mlNotHighOpt(QStringLiteral("ml-not-slide-high"),
        QStringLiteral("not_slide high-confidence threshold."), QStringLiteral("f"));
    QCommandLineOption mlNotLowOpt(QStringLiteral("ml-not-slide-low"),
        QStringLiteral("not_slide low-confidence threshold."), QStringLiteral("f"));
    QCommandLineOption mlMaybeHighOpt(QStringLiteral("ml-maybe-slide-high"),
        QStringLiteral("may_be_slide high-confidence threshold."), QStringLiteral("f"));
    QCommandLineOption mlMaybeLowOpt(QStringLiteral("ml-maybe-slide-low"),
        QStringLiteral("may_be_slide low-confidence threshold."), QStringLiteral("f"));
    QCommandLineOption mlSlideMaxOpt(QStringLiteral("ml-slide-max"),
        QStringLiteral("Max slide probability for medium-confidence deletion."), QStringLiteral("f"));
    QCommandLineOption mlDelMaybeOpt(QStringLiteral("ml-delete-maybe-slides"),
        QStringLiteral("Delete may_be_slide images: true|false."), QStringLiteral("bool"));

    QCommandLineOption jsonOpt(QStringLiteral("json"),
        QStringLiteral("Emit NDJSON / JSON Lines events to stdout (and JSON errors to stderr) "
                       "instead of human-readable text. Suppresses the progress bar. "
                       "Designed for child_process.spawn integration (e.g. Electron)."));

    parser.addOption(videoOpt);
    parser.addOption(outputOpt);
    parser.addOption(ssimOpt);
    parser.addOption(downsampleOpt);
    parser.addOption(dsWidthOpt);
    parser.addOption(dsHeightOpt);
    parser.addOption(chunkOpt);
    parser.addOption(jpegOpt);
    parser.addOption(phashRedundantOpt);
    parser.addOption(phashExclusionOpt);
    parser.addOption(phashExclusionHashesOpt);
    parser.addOption(hammingOpt);
    parser.addOption(mlOpt);
    parser.addOption(mlModelOpt);
    parser.addOption(mlEpOpt);
    parser.addOption(mlNotHighOpt);
    parser.addOption(mlNotLowOpt);
    parser.addOption(mlMaybeHighOpt);
    parser.addOption(mlMaybeLowOpt);
    parser.addOption(mlSlideMaxOpt);
    parser.addOption(mlDelMaybeOpt);
    parser.addOption(jsonOpt);

    if (!parser.parse(arguments)) {
        *errorOut = parser.errorText();
        return false;
    }

    // Resolve --json before any output so help/version respect it.
    m_jsonMode = parser.isSet(jsonOpt);

    if (parser.isSet(QStringLiteral("help"))) {
        if (m_jsonMode) {
            QJsonObject obj;
            obj.insert(QStringLiteral("text"), parser.helpText());
            emitEvent(QStringLiteral("help"), obj);
        } else {
            writeStdout(parser.helpText());
        }
        std::exit(EXIT_OK);
    }
    if (parser.isSet(QStringLiteral("version"))) {
        if (m_jsonMode) {
            QJsonObject obj;
            obj.insert(QStringLiteral("appVersion"), QCoreApplication::applicationVersion());
            emitEvent(QStringLiteral("version"), obj);
        } else {
            writeStdout(QCoreApplication::applicationName() + QStringLiteral(" ") + QCoreApplication::applicationVersion());
        }
        std::exit(EXIT_OK);
    }

    if (!parser.isSet(videoOpt)) {
        *errorOut = QStringLiteral("--video is required");
        return false;
    }
    if (!parser.isSet(outputOpt)) {
        *errorOut = QStringLiteral("--output is required");
        return false;
    }

    m_videoPath = parser.value(videoOpt);
    m_outputDir = parser.value(outputOpt);

    bool ok = true;
    if (parser.isSet(ssimOpt)) {
        double v = parser.value(ssimOpt).toDouble(&ok);
        if (!ok) { *errorOut = QStringLiteral("--ssim-threshold must be numeric"); return false; }
        m_config.ssimPreset = SSIMPreset::Custom;
        m_config.customSSIMThreshold = v;
    }
    if (parser.isSet(downsampleOpt)) {
        bool b;
        if (!parseBoolFlag(parser.value(downsampleOpt), &b)) {
            *errorOut = QStringLiteral("--enable-downsampling expects true|false");
            return false;
        }
        m_config.enableDownsampling = b;
    }
    if (parser.isSet(dsWidthOpt)) {
        int v = parser.value(dsWidthOpt).toInt(&ok);
        if (!ok || v <= 0) { *errorOut = QStringLiteral("--downsample-width must be a positive integer"); return false; }
        m_config.downsampleWidth = v;
    }
    if (parser.isSet(dsHeightOpt)) {
        int v = parser.value(dsHeightOpt).toInt(&ok);
        if (!ok || v <= 0) { *errorOut = QStringLiteral("--downsample-height must be a positive integer"); return false; }
        m_config.downsampleHeight = v;
    }
    if (parser.isSet(chunkOpt)) {
        int v = parser.value(chunkOpt).toInt(&ok);
        if (!ok || v <= 0) { *errorOut = QStringLiteral("--chunk-size must be a positive integer"); return false; }
        m_config.chunkSize = v;
    }
    if (parser.isSet(jpegOpt)) {
        int v = parser.value(jpegOpt).toInt(&ok);
        if (!ok || v < 1 || v > 100) { *errorOut = QStringLiteral("--jpeg-quality must be 1..100"); return false; }
        m_config.jpegQuality = v;
    }

    m_phashRedundant = parser.isSet(phashRedundantOpt);
    if (parser.isSet(phashExclusionOpt)) {
        m_phashExclusion = true;
    }
    if (parser.isSet(phashExclusionHashesOpt)) {
        m_phashExclusion = true;
        m_exclusionListOverridden = true;
        QString hashErr;
        if (!parseExclusionHashes(parser.value(phashExclusionHashesOpt), &hashErr)) {
            *errorOut = hashErr;
            return false;
        }
    }
    if (parser.isSet(hammingOpt)) {
        int v = parser.value(hammingOpt).toInt(&ok);
        if (!ok || v < 0) { *errorOut = QStringLiteral("--hamming-threshold must be a non-negative integer"); return false; }
        m_config.hammingThreshold = v;
    }

    m_mlClassify = parser.isSet(mlOpt);
    if (parser.isSet(mlModelOpt)) {
        m_config.mlModelPath = parser.value(mlModelOpt);
        if (m_config.mlModelPath.isEmpty()) {
            m_config.mlModelPath = QStringLiteral(":/models/resources/models/slide_classifier_mobilenetv4_v1.onnx");
        }
    }
    if (parser.isSet(mlEpOpt)) {
        m_config.mlExecutionProvider = parser.value(mlEpOpt);
    }
    if (parser.isSet(mlNotHighOpt)) {
        float v = parser.value(mlNotHighOpt).toFloat(&ok);
        if (!ok) { *errorOut = QStringLiteral("--ml-not-slide-high must be numeric"); return false; }
        m_config.mlNotSlideHighThreshold = v;
    }
    if (parser.isSet(mlNotLowOpt)) {
        float v = parser.value(mlNotLowOpt).toFloat(&ok);
        if (!ok) { *errorOut = QStringLiteral("--ml-not-slide-low must be numeric"); return false; }
        m_config.mlNotSlideLowThreshold = v;
    }
    if (parser.isSet(mlMaybeHighOpt)) {
        float v = parser.value(mlMaybeHighOpt).toFloat(&ok);
        if (!ok) { *errorOut = QStringLiteral("--ml-maybe-slide-high must be numeric"); return false; }
        m_config.mlMaybeSlideHighThreshold = v;
    }
    if (parser.isSet(mlMaybeLowOpt)) {
        float v = parser.value(mlMaybeLowOpt).toFloat(&ok);
        if (!ok) { *errorOut = QStringLiteral("--ml-maybe-slide-low must be numeric"); return false; }
        m_config.mlMaybeSlideLowThreshold = v;
    }
    if (parser.isSet(mlSlideMaxOpt)) {
        float v = parser.value(mlSlideMaxOpt).toFloat(&ok);
        if (!ok) { *errorOut = QStringLiteral("--ml-slide-max must be numeric"); return false; }
        m_config.mlSlideMaxThreshold = v;
    }
    if (parser.isSet(mlDelMaybeOpt)) {
        bool b;
        if (!parseBoolFlag(parser.value(mlDelMaybeOpt), &b)) {
            *errorOut = QStringLiteral("--ml-delete-maybe-slides expects true|false");
            return false;
        }
        m_config.mlDeleteMaybeSlides = b;
    }

    return true;
}

bool CliRunner::parseExclusionHashes(const QString& csv, QString* errorOut)
{
    m_exclusionList.clear();
    const QStringList parts = csv.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        *errorOut = QStringLiteral("--phash-exclusion-hashes is empty");
        return false;
    }
    static const QRegularExpression hexRe(QStringLiteral("^[0-9a-fA-F]{64}$"));
    for (const QString& raw : parts) {
        QString hash = raw.trimmed();
        if (!hexRe.match(hash).hasMatch()) {
            *errorOut = QStringLiteral("invalid pHash (expected 64 hex chars): ") + hash;
            return false;
        }
        m_exclusionList.append(ExclusionEntry(QStringLiteral("CLI"), hash));
    }
    return true;
}

int CliRunner::runProcessingStage(QString* outSlidesDir, int* outSlideCount)
{
    VideoQueue queue;
    if (queue.addVideo(m_videoPath) < 0) {
        const QString msg = QStringLiteral("failed to enqueue video");
        if (m_jsonMode) {
            emitError(QStringLiteral("processing_failed"), msg, EXIT_PROCESSING_FAILED);
        } else {
            writeStderr(QStringLiteral("error: ") + msg);
        }
        return EXIT_PROCESSING_FAILED;
    }

    ProcessingThread thread(&queue);
    thread.updateConfig(m_config);
    m_activeThread = &thread;

    m_processingFinished = false;
    m_processingResultCode = EXIT_OK;
    m_processingSlideCount = 0;
    m_lastFramePercent = -100.0;
    m_progressBarActive = false;
    m_frameProgressTimer.start();

    QEventLoop loop;
    connect(&thread, &ProcessingThread::videoProcessingCompleted,
            this, &CliRunner::onVideoProcessingCompleted);
    connect(&thread, &ProcessingThread::videoProcessingError,
            this, &CliRunner::onVideoProcessingError);
    connect(&thread, &ProcessingThread::frameExtractionProgress,
            this, &CliRunner::onFrameExtractionProgress);
    connect(&thread, &ProcessingThread::videoInfoLogged,
            this, [this](int, const QString& info) {
                if (m_jsonMode) {
                    emitEvent(QStringLiteral("info"), parseVideoInfoString(info));
                } else {
                    writeStdout(QStringLiteral("[info] ") + info);
                }
            });
    connect(&thread, &ProcessingThread::processingStopped, &loop, &QEventLoop::quit);

    thread.startProcessing();
    loop.exec();

    thread.stopProcessing();
    thread.wait();
    finishProgressBar();
    m_activeThread = nullptr;

    if (g_cancelRequested.load()) {
        const int sig = g_cancelSignal.load();
        const int rc = (sig == SIGINT) ? EXIT_CANCELLED_SIGINT : EXIT_CANCELLED_SIGTERM;
        if (m_jsonMode && !m_cancelHandled) {
            QJsonObject obj;
            obj.insert(QStringLiteral("signal"),
                       sig == SIGINT ? QStringLiteral("SIGINT") : QStringLiteral("SIGTERM"));
            obj.insert(QStringLiteral("exitCode"), rc);
            emitEvent(QStringLiteral("cancelled"), obj);
            m_cancelHandled = true;
        } else if (!m_jsonMode && !m_cancelHandled) {
            writeStderr(QStringLiteral("cancelled by signal"));
            m_cancelHandled = true;
        }
        return rc;
    }

    if (m_processingResultCode != EXIT_OK) {
        return m_processingResultCode;
    }

    VideoQueueItem* item = queue.getVideo(0);
    if (!item || item->outputDirectory.isEmpty()) {
        const QString msg = QStringLiteral("processing produced no output directory");
        if (m_jsonMode) {
            emitError(QStringLiteral("processing_failed"), msg, EXIT_PROCESSING_FAILED);
        } else {
            writeStderr(QStringLiteral("error: ") + msg);
        }
        return EXIT_PROCESSING_FAILED;
    }

    *outSlidesDir = item->outputDirectory;
    *outSlideCount = m_processingSlideCount;

    if (m_jsonMode) {
        QJsonObject obj;
        obj.insert(QStringLiteral("count"), m_processingSlideCount);
        obj.insert(QStringLiteral("slidesDir"), item->outputDirectory);
        emitEvent(QStringLiteral("slides_extracted"), obj);
    }
    return EXIT_OK;
}

int CliRunner::runPostProcessingStage(const QString& slidesDir)
{
    PostProcessor processor;
    m_lastPostCurrent = -1;
    m_postStage = m_phashRedundant || m_phashExclusion
        ? QStringLiteral("phash") : QStringLiteral("ml");
    m_postProcessingFailed = false;
    m_postProgressTimer.start();

    connect(&processor, &PostProcessor::progressUpdated,
            this, &CliRunner::onPostProgressUpdated);
    connect(&processor, &PostProcessor::imageMovedToTrash,
            this, &CliRunner::onImageMovedToTrash);
    connect(&processor, &PostProcessor::mlClassificationStarted,
            this, &CliRunner::onMLClassificationStarted);
    connect(&processor, &PostProcessor::mlClassificationFailed,
            this, &CliRunner::onMLClassificationFailed);

    PostProcessingResult result = processor.processDirectory(
        slidesDir,
        m_phashRedundant,
        m_phashExclusion,
        m_config.hammingThreshold,
        m_exclusionList,
        m_mlClassify,
        m_config.mlModelPath,
        m_config.mlNotSlideHighThreshold,
        m_config.mlNotSlideLowThreshold,
        m_config.mlMaybeSlideHighThreshold,
        m_config.mlMaybeSlideLowThreshold,
        m_config.mlSlideMaxThreshold,
        m_config.mlDeleteMaybeSlides,
        m_config.mlExecutionProvider,
        /*useApplicationTrash=*/true,
        /*baseOutputDir=*/m_outputDir);

    if (m_jsonMode) {
        QJsonObject obj;
        obj.insert(QStringLiteral("totalRemoved"), result.totalRemoved);
        obj.insert(QStringLiteral("removedByPHash"), result.removedByPHash);
        obj.insert(QStringLiteral("removedByML"), result.removedByML);
        emitEvent(QStringLiteral("post_complete"), obj);
    } else {
        writeStdout(QString(QStringLiteral("[post] removed=%1 (pHash=%2, ML=%3)"))
                        .arg(result.totalRemoved)
                        .arg(result.removedByPHash)
                        .arg(result.removedByML));
    }

    // Surface ML-only failures via the previously-unused exit code.
    if (m_postProcessingFailed && m_mlClassify && !m_phashRedundant && !m_phashExclusion) {
        return EXIT_POSTPROCESSING_FAILED;
    }
    return EXIT_OK;
}

void CliRunner::onVideoProcessingCompleted(int /*videoIndex*/, int slidesExtracted)
{
    m_processingSlideCount = slidesExtracted;
    m_processingResultCode = EXIT_OK;
    m_processingFinished = true;
    if (!m_jsonMode) {
        writeStdout(QString(QStringLiteral("[done] %1 slides extracted")).arg(slidesExtracted));
    }
    // In JSON mode we emit slides_extracted from runProcessingStage where the
    // resolved slidesDir is also available — keeps the event self-contained.
}

void CliRunner::onVideoProcessingError(int /*videoIndex*/, const QString& error)
{
    m_processingResultCode = EXIT_PROCESSING_FAILED;
    m_processingFinished = true;
    if (m_jsonMode) {
        emitError(QStringLiteral("processing_failed"), error, EXIT_PROCESSING_FAILED);
    } else {
        writeStderr(QStringLiteral("error: ") + error);
    }
}

void CliRunner::onFrameExtractionProgress(int videoIndex, double percentage)
{
    const bool atEnd = percentage >= 99.95;
    if (atEnd && m_lastFramePercent >= 99.95) {
        return;
    }
    if (!atEnd &&
        m_frameProgressTimer.elapsed() < 100 &&
        std::abs(percentage - m_lastFramePercent) < 0.5) {
        return;
    }
    m_frameProgressTimer.restart();
    m_lastFramePercent = percentage;
    if (m_jsonMode) {
        QJsonObject obj;
        obj.insert(QStringLiteral("percent"), percentage);
        obj.insert(QStringLiteral("videoIndex"), videoIndex);
        emitEvent(QStringLiteral("frame_progress"), obj);
    } else {
        writeProgressBar(percentage);
        if (atEnd) {
            finishProgressBar();
        }
    }
}

void CliRunner::onPostProgressUpdated(int current, int total)
{
    if (current == m_lastPostCurrent) return;
    if (m_postProgressTimer.elapsed() < 250 && current != total) return;
    m_postProgressTimer.restart();
    m_lastPostCurrent = current;
    if (m_jsonMode) {
        QJsonObject obj;
        obj.insert(QStringLiteral("current"), current);
        obj.insert(QStringLiteral("total"), total);
        obj.insert(QStringLiteral("stage"), m_postStage);
        emitEvent(QStringLiteral("post_progress"), obj);
    } else {
        writeStdout(QString(QStringLiteral("[post] %1/%2")).arg(current).arg(total));
    }
}

void CliRunner::onImageMovedToTrash(const QString& filePath, const QString& reason)
{
    if (m_jsonMode) {
        QJsonObject obj;
        obj.insert(QStringLiteral("file"), QFileInfo(filePath).fileName());
        obj.insert(QStringLiteral("path"), filePath);
        obj.insert(QStringLiteral("reason"), reason);
        obj.insert(QStringLiteral("category"), trashCategoryForReason(reason));
        emitEvent(QStringLiteral("trash"), obj);
    } else {
        writeStdout(QString(QStringLiteral("[trash] %1 (%2)")).arg(QFileInfo(filePath).fileName(), reason));
    }
}

void CliRunner::onMLClassificationStarted(const QString& executionProvider)
{
    m_postStage = QStringLiteral("ml");
    if (m_jsonMode) {
        QJsonObject obj;
        obj.insert(QStringLiteral("executionProvider"), executionProvider);
        emitEvent(QStringLiteral("ml_started"), obj);
    } else {
        writeStdout(QStringLiteral("[ml] execution provider: ") + executionProvider);
    }
}

void CliRunner::onMLClassificationFailed(const QString& errorMessage)
{
    m_postProcessingFailed = true;
    if (m_jsonMode) {
        QJsonObject obj;
        obj.insert(QStringLiteral("message"), errorMessage);
        emitEvent(QStringLiteral("ml_failed"), obj, /*toStderr=*/true);
    } else {
        writeStderr(QStringLiteral("[ml] failed: ") + errorMessage);
    }
}
