#ifndef PROCESSINGTHREAD_H
#define PROCESSINGTHREAD_H

#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QTimer>
#include <memory>
#include "videoprocessor.h"
#include "hardwaredecoder.h"
#include "slidedetector.h"
#include "configmanager.h"
#include "videoqueue.h"
#include "chunkprocessor.h"

class ProcessingThread : public QThread
{
    Q_OBJECT

public:
    explicit ProcessingThread(VideoQueue* videoQueue, QObject *parent = nullptr);
    ~ProcessingThread();

    /**
     * Start processing the queue
     */
    void startProcessing();

    /**
     * Pause processing (will finish current video then stop)
     */
    void pauseProcessing();

    /**
     * Stop processing immediately and cleanup
     */
    void stopProcessing();

    /**
     * Force stop processing immediately, killing all running processes
     * This will interrupt FFmpeg operations and mark current video as error
     */
    void forceStop();

    /**
     * Check if thread is currently processing
     * @return true if processing
     */
    bool isProcessing() const;

    /**
     * Update configuration for processing
     * @param config New configuration
     */
    void updateConfig(const AppConfig& config);

signals:
    void processingStarted();
    void processingPaused();
    void processingStopped();
    void videoProcessingStarted(int videoIndex);
    void videoProcessingCompleted(int videoIndex, int slidesExtracted);
    void videoProcessingError(int videoIndex, const QString& error);
    void frameExtractionProgress(int videoIndex, double percentage);
    void ssimCalculationProgress(int videoIndex, int current, int total);
    void slideDetectionProgress(int videoIndex, int current, int total);
    void videoInfoLogged(int videoIndex, const QString& info);

protected:
    void run() override;

private slots:
    void onFrameExtracted(int frameNumber, int totalFrames);
    void onExtractionProgress(double percentage);
    void onExtractionError(const QString& error);
    void onSSIMCalculationProgress(int current, int total);
    void onSlideDetectionProgress(int current, int total);
    void onDetectionError(const QString& error);

private:
    /**
     * Process a single video
     * @param videoIndex Index of video in queue
     * @return true if successful
     */
    bool processVideo(int videoIndex);

    /**
     * Process video with chunk-based memory-limited approach
     * @param video Video item to process
     * @param videoIndex Index of video in queue
     * @return true if successful
     */
    bool processVideoWithChunks(VideoQueueItem* video, int videoIndex);


    /**
     * Producer thread function for frame extraction
     * @param videoPath Path to video file
     * @param chunkSize Size of each chunk
     */
    void producerThread(const std::string& videoPath, int chunkSize);

    /**
     * Consumer thread function for slide detection and processing
     * @param videoIndex Index of video in queue
     * @param outputDir Output directory for slides
     * @param videoName Video file name (without extension)
     */
    void consumerThread(int videoIndex, const QString& outputDir, const QString& videoName);

    /**
     * Create output directory for slides
     * @param videoPath Path to video file
     * @param baseOutputDir Base output directory
     * @return Output directory path
     */
    QString createOutputDirectory(const QString& videoPath, const QString& baseOutputDir);

    /**
     * Copy selected slides to output directory
     * @param selectedSlides Vector of selected slide paths
     * @param outputDir Output directory
     * @param videoName Video file name (without extension)
     * @return Number of slides copied
     */
    int copySlides(const std::vector<std::string>& selectedSlides,
                   const QString& outputDir,
                   const QString& videoName);

    /**
     * Save selected slides from in-memory frames to output directory
     * @param selectedIndices Vector of selected frame indices
     * @param frames Vector of all frames
     * @param outputDir Output directory
     * @param videoName Video file name (without extension)
     * @return Number of slides saved
     */
    int saveSlidesFromFrames(const std::vector<int>& selectedIndices,
                           const std::vector<cv::Mat>& frames,
                           const QString& outputDir,
                           const QString& videoName);

    /**
     * Cleanup temporary files
     * @param tempFiles Vector of temporary file paths
     */
    void cleanupTempFiles(const std::vector<std::string>& tempFiles);

    /**
     * Get temporary directory for frame extraction
     * @return Temporary directory path
     */
    QString getTempDirectory();

    VideoQueue* m_videoQueue;
    std::unique_ptr<VideoProcessor> m_videoProcessor;
    std::unique_ptr<SlideDetector> m_slideDetector;
    AppConfig m_config;

    mutable QMutex m_mutex;
    QWaitCondition m_condition;
    bool m_shouldStop;
    bool m_shouldPause;
    bool m_isProcessing;

    int m_currentVideoIndex;
    QString m_currentError;

    // Producer-consumer model for chunk-based processing
    ProcessingState m_processingState;
    QMutex m_queueMutex;
    QWaitCondition m_queueNotFull;
    QWaitCondition m_queueNotEmpty;
    std::unique_ptr<FrameChunk> m_sharedQueue;  // Capacity = 1 queue
    bool m_producerFinished;
    int m_totalFramesExtracted;  // Total frames that will be extracted (set by producer)
    double m_currentExtractionProgress;  // Current frame extraction progress (0-100)

    // Current decoder for cancellation support
    HardwareDecoder* m_currentDecoder;
    QMutex m_decoderMutex;

    // Pause between videos (in milliseconds)
    static const int INTER_VIDEO_PAUSE_MS = 2000;
};

#endif // PROCESSINGTHREAD_H