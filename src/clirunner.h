#ifndef CLIRUNNER_H
#define CLIRUNNER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QElapsedTimer>
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
};

#endif // CLIRUNNER_H
