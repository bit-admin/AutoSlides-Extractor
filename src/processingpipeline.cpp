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

    // A pHash duplicate must also be the same slide by the detector's SSIM test.
    DuplicateSSIMCheck duplicateCheck;
    duplicateCheck.ssimThreshold = ConfigManager::getSSIMThreshold(config.ssimPreset,
                                                                   config.customSSIMThreshold);
    duplicateCheck.enableDownsampling = config.enableDownsampling;
    duplicateCheck.downsampleWidth = config.downsampleWidth;
    duplicateCheck.downsampleHeight = config.downsampleHeight;

    // The single place the processDirectory call is assembled from Request + AppConfig.
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
        request.baseOutputDir,
        config.mlAutoCropMaybeSlides,
        config.mlPostCropDedup,
        config.autoCrop,
        config.jpegQuality,
        duplicateCheck);
}
