#ifndef CLIRUNNER_H
#define CLIRUNNER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QElapsedTimer>
#include <QJsonObject>
#include "configmanager.h"

#ifndef AUTOSLIDES_ELECTRON_HELPER
#include "videoexporter.h"
#include "pdfexporter.h"
#include "postprocessor.h"
#endif

class ProcessingThread;

class CliRunner : public QObject
{
    Q_OBJECT

public:
    explicit CliRunner(QObject* parent = nullptr);

    int run(const QStringList& arguments);

private:
    void requestCancel();
    void onVideoProcessingCompleted(int videoIndex, int slidesExtracted);
    void onVideoProcessingError(int videoIndex, const QString& error);
    void onFrameExtractionProgress(int videoIndex, double percentage);

    bool parseArgs(const QStringList& arguments, QString* errorOut);
    int runProcessingStage(QString* outSlidesDir, int* outSlideCount);

    void writeStdout(const QString& line);
    void writeStderr(const QString& line);
    void writeProgressBar(double percentage);
    void finishProgressBar();

    void emitEvent(const QString& event, QJsonObject fields, bool toStderr = false);
    void emitError(const QString& category, const QString& message, int exitCode);
    QJsonObject parseVideoInfoString(const QString& raw) const;

    void installSignalHandlers();
    void uninstallSignalHandlers();
    void attachWindowsConsole();

    AppConfig m_config;
    QString m_videoPath;
    QString m_outputDir;

    bool m_phashRedundant = false;
    bool m_phashExclusion = false;
    bool m_mlClassify = false;

    int m_processingResultCode = 0;
    int m_processingSlideCount = 0;
    bool m_processingFinished = false;

    QElapsedTimer m_frameProgressTimer;
    double m_lastFramePercent = -100.0;
    bool m_progressBarActive = false;
    bool m_jsonMode = false;
    bool m_cancelHandled = false;         // true once we've emitted the cancelled event
    ProcessingThread* m_activeThread = nullptr; // set during runProcessingStage

#ifndef AUTOSLIDES_ELECTRON_HELPER
    void onPostProgressUpdated(int current, int total);
    void onImageMovedToTrash(const QString& filePath, const QString& reason);
    void onMLClassificationStarted(const QString& executionProvider);
    void onMLClassificationFailed(const QString& errorMessage);
    bool parseExclusionHashes(const QString& csv, QString* errorOut);
    int runPostProcessingStage(const QString& slidesDir);
    int runVideoExportStage(const QString& slidesDir);
    int runPdfExportStage(const QString& slidesDir);
    QString trashCategoryForReason(const QString& reason) const;

    bool m_exclusionListOverridden = false;
    bool m_exportVideo = false;
    VideoExportOptions m_videoOptions;   // Slides Export defaults + --video-* flags
    bool m_exportPdf = false;
    PdfExportOptions m_pdfOptions;       // Slides Export defaults + --pdf-* flags
    QList<ExclusionEntry> m_exclusionList;
    QElapsedTimer m_postProgressTimer;
    int m_lastPostCurrent = -1;
    bool m_postProcessingFailed = false;
    QString m_postStage = QStringLiteral("phash");
#endif
};

#endif // CLIRUNNER_H
