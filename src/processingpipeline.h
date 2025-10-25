#ifndef PROCESSINGPIPELINE_H
#define PROCESSINGPIPELINE_H

#include <QObject>
#include <QString>
#include <QList>

#include "postprocessor.h"   // PostProcessingResult, ExclusionEntry
#include "configmanager.h"   // AppConfig

/**
 * @brief Single owner of the post-processing orchestration.
 *
 * Before this class existed, MainWindow (x2), CliRunner and ReviewSlidesDialog
 * each hand-wrote the same ~16-argument PostProcessor::processDirectory(...)
 * call plus its signal wiring. ProcessingPipeline is the one place that builds
 * that call from an AppConfig (for the ML thresholds / model / provider) plus a
 * small per-caller Request (for which phases are enabled and the exclusion
 * list). It re-emits PostProcessor's signals so callers connect to a stable
 * surface instead of re-wiring PostProcessor internals.
 */
class ProcessingPipeline : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Per-call inputs that vary between callers. Everything else (Hamming
     * threshold, ML thresholds, model path, execution provider, delete-maybe
     * flag) is taken from the AppConfig passed to runPostProcessing().
     */
    struct Request {
        QString imageDir;                    // directory of slide_*.jpg to process
        bool deleteRedundant = true;         // pHash duplicate phase
        bool compareExcluded = true;         // pHash exclusion-list phase
        bool enableML = true;                // ML classification phase
        QList<ExclusionEntry> exclusionList; // hashes for the exclusion phase
        bool useApplicationTrash = true;     // app trash vs system trash
        QString baseOutputDir;               // base dir for the application trash
    };

    explicit ProcessingPipeline(QObject *parent = nullptr);

    /**
     * @brief Run pHash + ML post-processing on a single directory.
     * @param request Per-call phase flags and exclusion list.
     * @param config  Source of the Hamming + ML thresholds, model path, provider.
     * @return Breakdown of how many images were moved to trash.
     */
    PostProcessingResult runPostProcessing(const Request& request, const AppConfig& config);

signals:
    void progressUpdated(int current, int total);
    void imageMovedToTrash(const QString& filePath, const QString& reason);
    void mlClassificationStarted(const QString& executionProvider);
    void mlClassificationFailed(const QString& errorMessage);
};

#endif // PROCESSINGPIPELINE_H
