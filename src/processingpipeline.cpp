#include "processingpipeline.h"

ProcessingPipeline::ProcessingPipeline(QObject *parent)
    : QObject(parent)
{
}

PostProcessingResult ProcessingPipeline::runPostProcessing(const Request& request,
                                                           const AppConfig& config)
{
    PostProcessor processor;

    // Forward PostProcessor's signals through this pipeline so callers wire to a
    // stable surface. Connections live only as long as this scope / processor.
    connect(&processor, &PostProcessor::progressUpdated,
            this, &ProcessingPipeline::progressUpdated);
    connect(&processor, &PostProcessor::imageMovedToTrash,
            this, &ProcessingPipeline::imageMovedToTrash);
    connect(&processor, &PostProcessor::mlClassificationStarted,
            this, &ProcessingPipeline::mlClassificationStarted);
    connect(&processor, &PostProcessor::mlClassificationFailed,
            this, &ProcessingPipeline::mlClassificationFailed);

    // The single place the ~16-argument processDirectory call is assembled.
    return processor.processDirectory(
        request.imageDir,
        request.deleteRedundant,
        request.compareExcluded,
        config.hammingThreshold,
        request.exclusionList,
        request.enableML,
        config.mlModelPath,
        config.mlNotSlideHighThreshold,
        config.mlNotSlideLowThreshold,
        config.mlMaybeSlideHighThreshold,
        config.mlMaybeSlideLowThreshold,
        config.mlSlideMaxThreshold,
        config.mlDeleteMaybeSlides,
        config.mlExecutionProvider,
        request.useApplicationTrash,
        request.baseOutputDir);
}
