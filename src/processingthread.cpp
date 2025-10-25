#include "processingthread.h"
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDebug>
#include <QElapsedTimer>
#include <thread>
#include <functional>

ProcessingThread::ProcessingThread(VideoQueue* videoQueue, QObject *parent)
    : QThread(parent),
      m_videoQueue(videoQueue),
      m_shouldStop(false),
      m_shouldPause(false),
      m_isProcessing(false),
      m_currentVideoIndex(-1),
      m_producerFinished(false)
{
    m_videoProcessor = std::make_unique<VideoProcessor>(this);
    m_slideDetector = std::make_unique<SlideDetector>(this);

    // Connect signals
    connect(m_videoProcessor.get(), &VideoProcessor::frameExtracted,
            this, &ProcessingThread::onFrameExtracted);
    connect(m_videoProcessor.get(), &VideoProcessor::extractionProgress,
            this, &ProcessingThread::onExtractionProgress);
    connect(m_videoProcessor.get(), &VideoProcessor::extractionError,
            this, &ProcessingThread::onExtractionError);

    connect(m_slideDetector.get(), &SlideDetector::ssimCalculationProgress,
            this, &ProcessingThread::onSSIMCalculationProgress);
    connect(m_slideDetector.get(), &SlideDetector::slideDetectionProgress,
            this, &ProcessingThread::onSlideDetectionProgress);
    connect(m_slideDetector.get(), &SlideDetector::detectionError,
            this, &ProcessingThread::onDetectionError);
}

ProcessingThread::~ProcessingThread()
{
    stopProcessing();
    wait();
}

void ProcessingThread::startProcessing()
{
    QMutexLocker locker(&m_mutex);
    m_shouldStop = false;
    m_shouldPause = false;

    if (!isRunning()) {
        start();
    } else {
        m_condition.wakeOne();
    }
}

void ProcessingThread::pauseProcessing()
{
    QMutexLocker locker(&m_mutex);
    m_shouldPause = true;
}

void ProcessingThread::stopProcessing()
{
    QMutexLocker locker(&m_mutex);
    m_shouldStop = true;
    m_condition.wakeOne();
}

bool ProcessingThread::isProcessing() const
{
    QMutexLocker locker(&m_mutex);
    return m_isProcessing;
}

void ProcessingThread::updateConfig(const AppConfig& config)
{
    QMutexLocker locker(&m_mutex);
    m_config = config;
}

void ProcessingThread::run()
{
    {
        QMutexLocker locker(&m_mutex);
        m_isProcessing = true;
    }

    emit processingStarted();

    while (true) {
        {
            QMutexLocker locker(&m_mutex);
            if (m_shouldStop) {
                break;
            }

            if (m_shouldPause) {
                m_isProcessing = false;
                emit processingPaused();
                m_condition.wait(&m_mutex);
                m_isProcessing = true;
                m_shouldPause = false;
                continue;
            }
        }

        // Get next video to process
        int videoIndex = m_videoQueue->getNextToProcess();
        if (videoIndex == -1) {
            // No more videos to process
            break;
        }

        // Process the video
        bool success = processVideo(videoIndex);

        if (!success) {
            QMutexLocker locker(&m_mutex);
            if (m_shouldStop || m_shouldPause) {
                break;
            }
        }

        // Pause between videos
        QThread::msleep(INTER_VIDEO_PAUSE_MS);
    }

    {
        QMutexLocker locker(&m_mutex);
        m_isProcessing = false;
    }

    emit processingStopped();
}

bool ProcessingThread::processVideo(int videoIndex)
{
    VideoQueueItem* video = m_videoQueue->getVideo(videoIndex);
    if (!video) {
        return false;
    }

    // Always use chunk-based processing for memory optimization
    return processVideoWithChunks(video, videoIndex);
}


bool ProcessingThread::processVideoWithChunks(VideoQueueItem* video, int videoIndex)
{
    m_currentVideoIndex = videoIndex;
    m_currentError.clear();

    emit videoProcessingStarted(videoIndex);

    QElapsedTimer totalTimer;
    totalTimer.start();

    try {
        // Step 1: Analyze video and display info immediately
        m_videoQueue->updateStatus(videoIndex, ProcessingStatus::FFmpegHandling);

        // Create a temporary decoder just to get video information quickly
        HardwareDecoder tempDecoder;
        if (!tempDecoder.openVideo(video->filePath.toStdString())) {
            throw std::runtime_error("Failed to open video for analysis");
        }

        // Get video information and hardware acceleration method
        HardwareDecoder::VideoInfo videoInfo = tempDecoder.getVideoInfo();
        QString hwMethod = QString::fromStdString(tempDecoder.getHardwareAccelerationMethod());

        // Log video information immediately
        QString infoLog = QString("Video Info - Resolution: %1x%2, Duration: %3s, Frame Rate: %4fps, I-Frame Interval: %5s, Screen Recording: %6, Decoder: %7 [Memory-Limited Mode]")
                         .arg(videoInfo.width)
                         .arg(videoInfo.height)
                         .arg(videoInfo.duration, 0, 'f', 1)
                         .arg(videoInfo.frameRate, 0, 'f', 2)
                         .arg(videoInfo.avgIFrameInterval, 0, 'f', 2)
                         .arg(videoInfo.isScreenRecording ? "Yes" : "No")
                         .arg(hwMethod);
        emit videoInfoLogged(videoIndex, infoLog);

        // Close temporary decoder
        tempDecoder.close();

        // Step 2: Initialize processing state for chunk-based processing
        m_processingState.reset();
        m_producerFinished = false;
        m_sharedQueue.reset();

        // Prepare output directory
        QString outputDir = createOutputDirectory(video->filePath, m_config.outputDirectory);
        QFileInfo videoFileInfo(video->filePath);
        QString videoName = videoFileInfo.baseName();

        // Step 3: Start producer-consumer threads
        std::thread producer(&ProcessingThread::producerThread, this,
                           video->filePath.toStdString(),
                           m_config.chunkSize);

        std::thread consumer(&ProcessingThread::consumerThread, this,
                           videoIndex, outputDir, videoName);

        // Wait for both threads to complete
        producer.join();
        consumer.join();

        // Check for errors during processing
        {
            QMutexLocker locker(&m_mutex);
            if (m_shouldStop || m_shouldPause) {
                return false;
            }
            if (!m_currentError.isEmpty()) {
                throw std::runtime_error(m_currentError.toStdString());
            }
        }

        // Step 4: Final statistics and completion
        int slidesSaved = static_cast<int>(m_processingState.savedSlideIndices.size());
        double totalTime = totalTimer.elapsed() / 1000.0;
        m_videoQueue->updateStatistics(videoIndex, slidesSaved, totalTime);
        m_videoQueue->updateStatus(videoIndex, ProcessingStatus::Completed);

        emit videoProcessingCompleted(videoIndex, slidesSaved);
        return true;

    } catch (const std::exception& e) {
        QString errorMsg = QString::fromStdString(e.what());
        m_videoQueue->setError(videoIndex, errorMsg);
        emit videoProcessingError(videoIndex, errorMsg);
        return false;
    }
}

QString ProcessingThread::createOutputDirectory(const QString& videoPath, const QString& baseOutputDir)
{
    QFileInfo videoInfo(videoPath);
    QString videoName = videoInfo.baseName();
    QString outputDir = baseOutputDir + "/slides_" + videoName;

    QDir dir;
    if (!dir.mkpath(outputDir)) {
        throw std::runtime_error("Failed to create output directory: " + outputDir.toStdString());
    }

    return outputDir;
}

int ProcessingThread::copySlides(const std::vector<std::string>& selectedSlides,
                                const QString& outputDir,
                                const QString& videoName)
{
    int copiedCount = 0;

    for (size_t i = 0; i < selectedSlides.size(); ++i) {
        QString sourcePath = QString::fromStdString(selectedSlides[i]);
        QString fileName = QString("slide_%1_%2.jpg")
                          .arg(videoName)
                          .arg(i + 1, 3, 10, QChar('0'));
        QString destPath = outputDir + "/" + fileName;

        if (QFile::copy(sourcePath, destPath)) {
            copiedCount++;
        }
    }

    return copiedCount;
}

int ProcessingThread::saveSlidesFromFrames(const std::vector<int>& selectedIndices,
                                         const std::vector<cv::Mat>& frames,
                                         const QString& outputDir,
                                         const QString& videoName)
{
    int savedCount = 0;

    for (size_t i = 0; i < selectedIndices.size(); ++i) {
        int frameIndex = selectedIndices[i];
        if (frameIndex >= 0 && frameIndex < static_cast<int>(frames.size())) {
            QString fileName = QString("slide_%1_%2.jpg")
                              .arg(videoName)
                              .arg(i + 1, 3, 10, QChar('0'));
            QString filePath = outputDir + "/" + fileName;

            // Save the OpenCV Mat as JPEG
            std::vector<int> compression_params;
            compression_params.push_back(cv::IMWRITE_JPEG_QUALITY);
            compression_params.push_back(95); // High quality

            if (cv::imwrite(filePath.toStdString(), frames[frameIndex], compression_params)) {
                savedCount++;
            }
        }
    }

    return savedCount;
}

void ProcessingThread::cleanupTempFiles(const std::vector<std::string>& tempFiles)
{
    m_videoProcessor->cleanupFrames(tempFiles);

    // Also try to remove the temporary directory if it's empty
    if (!tempFiles.empty()) {
        QString tempDir = QFileInfo(QString::fromStdString(tempFiles[0])).absolutePath();
        QDir dir(tempDir);
        if (dir.isEmpty()) {
            dir.rmdir(tempDir);
        }
    }
}

QString ProcessingThread::getTempDirectory()
{
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    tempDir += "/AutoSlidesExtractor_" + QString::number(QDateTime::currentMSecsSinceEpoch());

    QDir dir;
    if (!dir.mkpath(tempDir)) {
        throw std::runtime_error("Failed to create temporary directory");
    }

    return tempDir;
}

void ProcessingThread::producerThread(const std::string& videoPath, int chunkSize)
{
    try {
        // Create hardware decoder for chunk-based extraction
        HardwareDecoder decoder;
        if (!decoder.openVideo(videoPath)) {
            QMutexLocker locker(&m_mutex);
            m_currentError = "Failed to open video for chunk extraction";
            return;
        }

        // Define chunk callback that puts chunks into the shared queue
        auto chunkCallback = [this](const std::vector<cv::Mat>& frames, int startOffset, bool isLastChunk) {
            // Check for stop/pause conditions
            {
                QMutexLocker locker(&m_mutex);
                if (m_shouldStop || m_shouldPause) {
                    return;
                }
            }

            // Create frame chunk
            auto chunk = std::make_unique<FrameChunk>(frames, startOffset, isLastChunk);

            // Wait for queue to be empty (capacity = 1)
            {
                QMutexLocker locker(&m_queueMutex);
                while (m_sharedQueue != nullptr) {
                    // Check for stop/pause while waiting
                    if (m_shouldStop || m_shouldPause) {
                        return;
                    }
                    m_queueNotFull.wait(&m_queueMutex);
                }

                // Put chunk into queue
                m_sharedQueue = std::move(chunk);
                m_queueNotEmpty.wakeOne();
            }
        };

        // Define progress callback for frame extraction progress
        // Note: HardwareDecoder calls with (currentTime, totalTime, percentage)
        auto progressCallback = [this](double currentTime, double totalTime, double percentage) {
            Q_UNUSED(currentTime)
            Q_UNUSED(totalTime)
            emit frameExtractionProgress(m_currentVideoIndex, percentage);
        };

        // Extract frames in chunks using the hardware decoder
        int totalFrames = decoder.extractFramesInChunks(
            chunkCallback,
            progressCallback,
            chunkSize,
            2.0  // Use 2 seconds as the ideal target interval
        );

        if (totalFrames <= 0) {
            QMutexLocker locker(&m_mutex);
            m_currentError = "No frames extracted from video";
            return;
        }

        // Mark producer as finished
        {
            QMutexLocker locker(&m_queueMutex);
            m_producerFinished = true;
            m_queueNotEmpty.wakeOne();  // Wake consumer in case it's waiting
        }

        decoder.close();

    } catch (const std::exception& e) {
        QMutexLocker locker(&m_mutex);
        m_currentError = QString("Producer thread error: %1").arg(e.what());

        // Mark producer as finished even on error
        QMutexLocker queueLocker(&m_queueMutex);
        m_producerFinished = true;
        m_queueNotEmpty.wakeOne();
    }
}

void ProcessingThread::consumerThread(int videoIndex, const QString& outputDir, const QString& videoName)
{
    try {
        int processedChunks = 0;

        while (true) {
            std::unique_ptr<FrameChunk> chunk;

            // Wait for a chunk to be available or producer to finish
            {
                QMutexLocker locker(&m_queueMutex);
                while (m_sharedQueue == nullptr && !m_producerFinished) {
                    // Check for stop/pause while waiting
                    if (m_shouldStop || m_shouldPause) {
                        return;
                    }
                    m_queueNotEmpty.wait(&m_queueMutex);
                }

                // If no chunk available and producer finished, we're done
                if (m_sharedQueue == nullptr && m_producerFinished) {
                    break;
                }

                // Take the chunk from the queue
                chunk = std::move(m_sharedQueue);
                m_sharedQueue.reset();
                m_queueNotFull.wakeOne();  // Wake producer
            }

            // Check for stop/pause before processing
            {
                QMutexLocker locker(&m_mutex);
                if (m_shouldStop || m_shouldPause) {
                    return;
                }
            }

            // Process the chunk
            if (chunk && !chunk->empty()) {
                // Update globalFrameOffset before processing
                m_processingState.globalFrameOffset = chunk->startOffset;

                // Update status for SSIM calculation
                m_videoQueue->updateStatus(videoIndex, ProcessingStatus::SSIMCalculating);

                // Get configuration parameters
                double ssimThreshold = ConfigManager::getSSIMThreshold(m_config.ssimPreset, m_config.customSSIMThreshold);
                int verificationCount = 3;  // Hardcoded as per PLAN.md requirements

                // Process chunk using slide detector
                SlideDetectionResult result = m_slideDetector->detectSlidesFromChunk(
                    chunk->frames,
                    m_processingState,
                    chunk->isLastChunk,
                    ssimThreshold,
                    verificationCount,
                    m_config.enableDownsampling,
                    m_config.downsampleWidth,
                    m_config.downsampleHeight
                );

                // Check for stop/pause after processing
                {
                    QMutexLocker locker(&m_mutex);
                    if (m_shouldStop || m_shouldPause) {
                        return;
                    }
                }

                // Save any new slides detected in this chunk
                if (!result.selectedSlideIndices.empty()) {
                    m_videoQueue->updateStatus(videoIndex, ProcessingStatus::ImageProcessing);

                    // Convert global indices to local indices for frame access
                    std::vector<cv::Mat> selectedFrames;
                    for (int globalIndex : result.selectedSlideIndices) {
                        // Convert global index to local index within current chunk
                        int localIndex = globalIndex - chunk->startOffset;
                        if (localIndex >= 0 && localIndex < static_cast<int>(chunk->frames.size())) {
                            selectedFrames.push_back(chunk->frames[localIndex]);
                        }
                    }

                    // Save slides with proper naming (continuing from previous slides)
                    int startSlideNumber = static_cast<int>(m_processingState.savedSlideIndices.size()) - static_cast<int>(selectedFrames.size()) + 1;
                    for (size_t i = 0; i < selectedFrames.size(); ++i) {
                        QString fileName = QString("slide_%1_%2.jpg")
                                          .arg(videoName)
                                          .arg(startSlideNumber + static_cast<int>(i), 3, 10, QChar('0'));
                        QString filePath = outputDir + "/" + fileName;

                        // Save the OpenCV Mat as JPEG
                        std::vector<int> compression_params;
                        compression_params.push_back(cv::IMWRITE_JPEG_QUALITY);
                        compression_params.push_back(95); // High quality

                        cv::imwrite(filePath.toStdString(), selectedFrames[i], compression_params);
                    }
                }

                processedChunks++;

                // Calculate slide processing progress based on processed frames
                int totalFramesProcessed = chunk->startOffset + static_cast<int>(chunk->frames.size());

                // For progress calculation, we need to estimate total frames
                // We can use the current frame rate and video duration, but for simplicity,
                // we'll use a chunk-based progress that updates as we process
                if (chunk->isLastChunk) {
                    // Final chunk - set progress to 100%
                    emit slideDetectionProgress(videoIndex, 100, 100);
                } else {
                    // Estimate progress based on processed chunks
                    // This is approximate but provides better user feedback
                    int estimatedProgress = std::min(95, (processedChunks * 80) / std::max(1, processedChunks + 1));
                    emit slideDetectionProgress(videoIndex, estimatedProgress, 100);
                }
            }

            // Chunk goes out of scope here, releasing memory
        }

    } catch (const std::exception& e) {
        QMutexLocker locker(&m_mutex);
        m_currentError = QString("Consumer thread error: %1").arg(e.what());
    }
}

// Slot implementations
void ProcessingThread::onFrameExtracted(int frameNumber, int totalFrames)
{
    Q_UNUSED(frameNumber)
    Q_UNUSED(totalFrames)
    // Frame extraction progress is handled by onExtractionProgress
}

void ProcessingThread::onExtractionProgress(double percentage)
{
    emit frameExtractionProgress(m_currentVideoIndex, percentage);
}

void ProcessingThread::onExtractionError(const QString& error)
{
    m_currentError = error;
}

void ProcessingThread::onSSIMCalculationProgress(int current, int total)
{
    emit ssimCalculationProgress(m_currentVideoIndex, current, total);
}

void ProcessingThread::onSlideDetectionProgress(int current, int total)
{
    emit slideDetectionProgress(m_currentVideoIndex, current, total);
}

void ProcessingThread::onDetectionError(const QString& error)
{
    m_currentError = error;
}