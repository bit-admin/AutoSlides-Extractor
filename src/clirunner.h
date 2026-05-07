#ifndef CLIRUNNER_H
#define CLIRUNNER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QElapsedTimer>
#include <QJsonObject>
#include "configmanager.h"
#include "postprocessor.h"

class ProcessingThread;
class VideoQueue;

class CliRunner : public QObject
{
    Q_OBJECT

public:
    explicit CliRunner(QObject* parent = nullptr);

    int run(const QStringList& arguments);

    // Cancellation entry point invoked from a posted metacall after a
    // SIGTERM/SIGINT handler fires. Public so QMetaObject::invokeMethod
    // can target it across threads.
    Q_INVOKABLE void requestCancel();

private slots:
    void onVideoProcessingCompleted(int videoIndex, int slidesExtracted);
    void onVideoProcessingError(int videoIndex, const QString& error);
    void onFrameExtractionProgress(int videoIndex, double percentage);

    void onPostProgressUpdated(int current, int total);
    void onImageMovedToTrash(const QString& filePath, const QString& reason);
    void onMLClassificationStarted(const QString& executionProvider);
    void onMLClassificationFailed(const QString& errorMessage);

private:
    bool parseArgs(const QStringList& arguments, QString* errorOut);
    bool parseExclusionHashes(const QString& csv, QString* errorOut);
    int runProcessingStage(QString* outSlidesDir, int* outSlideCount);
    int runPostProcessingStage(const QString& slidesDir);

    void writeStdout(const QString& line);
    void writeStderr(const QString& line);
    void writeProgressBar(double percentage);
    void finishProgressBar();

    void emitEvent(const QString& event, QJsonObject fields, bool toStderr = false);
    void emitError(const QString& category, const QString& message, int exitCode);
    QJsonObject parseVideoInfoString(const QString& raw) const;
    QString trashCategoryForReason(const QString& reason) const;

    void installSignalHandlers();
    void uninstallSignalHandlers();

    void attachWindowsConsole();

    AppConfig m_config;
    QString m_videoPath;
    QString m_outputDir;

    bool m_phashRedundant;
    bool m_phashExclusion;
    bool m_mlClassify;
    bool m_exclusionListOverridden;
    QList<ExclusionEntry> m_exclusionList;

    int m_processingResultCode;
    int m_processingSlideCount;
    bool m_processingFinished;

    QElapsedTimer m_frameProgressTimer;
    QElapsedTimer m_postProgressTimer;
    double m_lastFramePercent;
    int m_lastPostCurrent;
    bool m_progressBarActive;

    bool m_jsonMode;
    bool m_postProcessingFailed;
    QString m_postStage;          // "phash" | "ml"
    bool m_cancelHandled;         // true once we've emitted the cancelled event
    ProcessingThread* m_activeThread; // set during runProcessingStage; nullptr otherwise
};

#endif // CLIRUNNER_H
