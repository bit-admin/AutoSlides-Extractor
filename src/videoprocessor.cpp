#include "videoprocessor.h"
#include "memoryoptimizer.h"
#include <QDir>
#include <QDebug>
#include <QFileInfo>
#include <iostream>
#include <thread>

VideoProcessor::VideoProcessor(QObject *parent)
    : QObject(parent), m_initialized(true)
{
}

VideoProcessor::~VideoProcessor()
{
}

int VideoProcessor::getOptimalThreadCount()
{
    int coreCount = std::thread::hardware_concurrency();
    // Use cores - 1 to leave one core for system operations
    return std::max(1, coreCount - 1);
}


int VideoProcessor::extractFramesIntelligent(const std::string& videoPath,
                                           std::vector<cv::Mat>& frames,
                                           double targetInterval)
{
    frames.clear();

    if (!m_initialized) {
        emit extractionError("VideoProcessor not initialized");
        return -1;
    }

    // Create decoder and store it for later access
    m_lastDecoder = std::make_unique<HardwareDecoder>();
    if (!m_lastDecoder->openVideo(videoPath)) {
        emit extractionError(QString("Failed to open video: %1").arg(QString::fromStdString(m_lastDecoder->getLastError())));
        return -1;
    }

    // Emit video analysis results
    const auto& videoInfo = m_lastDecoder->getVideoInfo();
    emit videoAnalysisComplete(videoInfo);

    // Determine and emit sampling strategy
    auto strategy = m_lastDecoder->analyzeIFrameDistribution();
    emit samplingStrategyDetermined(strategy);

    // Warn user if I-frame interval is too large
    if (strategy == HardwareDecoder::SamplingStrategy::UseAllIFramesWarn) {
        emit extractionError(QString("Warning: Video has large I-frame intervals (%.1f seconds). "
                                   "This may result in fewer extracted frames than expected.")
                           .arg(videoInfo.avgIFrameInterval));
    }

    // Extract frames using intelligent sampling
    int frameCount = m_lastDecoder->extractFramesIntelligent(
        [&frames](const cv::Mat& mat, double timestamp, int frameNumber) {
            frames.push_back(mat); // Mat is already cloned in convertFrameToMat
        },
        [this](double currentTime, double totalDuration, double progress) {
            emit extractionProgress(progress);
        },
        targetInterval
    );

    if (frameCount < 0) {
        emit extractionError(QString("Frame extraction failed: %1").arg(QString::fromStdString(m_lastDecoder->getLastError())));
        return -1;
    }

    emit frameExtracted(frameCount, frameCount);
    emit extractionProgress(100.0);

    return frameCount;
}

int VideoProcessor::extractFramesToMemory(const std::string& videoPath,
                                        std::vector<cv::Mat>& frames,
                                        double intervalSeconds)
{
    frames.clear();

    if (!m_initialized) {
        emit extractionError("VideoProcessor not initialized");
        return -1;
    }

    HardwareDecoder decoder;
    if (!decoder.openVideo(videoPath)) {
        emit extractionError(QString("Failed to open video: %1").arg(QString::fromStdString(decoder.getLastError())));
        return -1;
    }

    // Extract frames at specified intervals
    int frameCount = decoder.extractFramesAtInterval(
        [&frames](const cv::Mat& mat, double timestamp, int frameNumber) {
            frames.push_back(mat); // Mat is already cloned in convertFrameToMat
        },
        [this](double currentTime, double totalDuration, double progress) {
            emit extractionProgress(progress);
        },
        intervalSeconds
    );

    if (frameCount < 0) {
        emit extractionError(QString("Frame extraction failed: %1").arg(QString::fromStdString(decoder.getLastError())));
        return -1;
    }

    emit frameExtracted(frameCount, frameCount);
    emit extractionProgress(100.0);

    return frameCount;
}

std::vector<std::string> VideoProcessor::extractFrames(const std::string& videoPath,
                                                     const std::string& outputDir,
                                                     double intervalSeconds)
{
    std::vector<std::string> framePaths;

    if (!m_initialized) {
        emit extractionError("VideoProcessor not initialized");
        return framePaths;
    }

    // Create output directory if it doesn't exist
    QDir dir;
    if (!dir.mkpath(QString::fromStdString(outputDir))) {
        emit extractionError("Failed to create output directory");
        return framePaths;
    }

    // Create decoder and extract frames
    HardwareDecoder decoder;
    if (!decoder.openVideo(videoPath)) {
        emit extractionError(QString("Failed to open video: %1").arg(QString::fromStdString(decoder.getLastError())));
        return framePaths;
    }

    // Get video info for progress tracking
    const auto& videoInfo = decoder.getVideoInfo();
    emit videoAnalysisComplete(videoInfo);

    int frameCounter = 0;
    int totalExpectedFrames = (videoInfo.duration > 0) ? (int)(videoInfo.duration / intervalSeconds) : 0;

    // Extract frames using new API and save to disk
    int frameCount = decoder.extractFramesAtInterval(
        [&](const cv::Mat& mat, double timestamp, int frameNumber) {
            // Generate frame filename
            QString frameFilename = QString("%1/frame_%2.jpg")
                .arg(QString::fromStdString(outputDir))
                .arg(frameCounter + 1, 6, 10, QChar('0'));

            // Save frame to disk
            if (cv::imwrite(frameFilename.toStdString(), mat)) {
                framePaths.push_back(frameFilename.toStdString());
                frameCounter++;
                emit frameExtracted(frameCounter, totalExpectedFrames);
            }
        },
        [this](double currentTime, double totalDuration, double progress) {
            emit extractionProgress(progress);
        },
        intervalSeconds
    );

    if (frameCount < 0) {
        emit extractionError(QString("Frame extraction failed: %1").arg(QString::fromStdString(decoder.getLastError())));
        return std::vector<std::string>(); // Return empty vector on error
    }

    emit extractionProgress(100.0);
    return framePaths;
}


HardwareDecoder::VideoInfo VideoProcessor::getVideoInfo(const std::string& videoPath)
{
    HardwareDecoder decoder;
    if (decoder.openVideo(videoPath)) {
        return decoder.getVideoInfo();
    }

    // Return empty info on error
    return HardwareDecoder::VideoInfo();
}

double VideoProcessor::getVideoDuration(const std::string& videoPath)
{
    auto info = getVideoInfo(videoPath);
    return info.duration > 0 ? info.duration : -1.0;
}

double VideoProcessor::getVideoFrameRate(const std::string& videoPath)
{
    auto info = getVideoInfo(videoPath);
    return info.frameRate > 0 ? info.frameRate : -1.0;
}

bool VideoProcessor::isHardwareAccelerationAvailable()
{
    return HardwareDecoder::isHardwareAccelerationAvailable();
}

std::string VideoProcessor::getHardwareAccelerationMethod() const
{
    if (m_lastDecoder) {
        return m_lastDecoder->getHardwareAccelerationMethod();
    }
    return "Unknown";
}


int VideoProcessor::extractFramesOptimized(const std::string& videoPath,
                                         std::vector<FrameBuffer>& frameBuffers,
                                         double targetInterval)
{
    frameBuffers.clear();

    // Create decoder instance
    m_lastDecoder = std::make_unique<HardwareDecoder>();

    if (!m_lastDecoder->openVideo(videoPath)) {
        emit extractionError(QString("Failed to open video: %1").arg(QString::fromStdString(m_lastDecoder->getLastError())));
        return -1;
    }

    // Get video information and emit signal
    HardwareDecoder::VideoInfo videoInfo = m_lastDecoder->getVideoInfo();
    emit videoAnalysisComplete(videoInfo);

    // Analyze I-frame distribution and emit strategy
    HardwareDecoder::SamplingStrategy strategy = m_lastDecoder->analyzeIFrameDistribution();
    emit samplingStrategyDetermined(strategy);

    // Warn user about potential issues with I-frame intervals
    if (videoInfo.avgIFrameInterval >= 4.0) {
        emit extractionError(QString("Warning: Large I-frame interval detected (%.1f seconds). "
                                   "This may result in fewer extracted frames than expected.")
                           .arg(videoInfo.avgIFrameInterval));
    }

    // Extract frames using optimized zero-copy method
    int frameCount = m_lastDecoder->extractFramesOptimized(
        [&frameBuffers](FrameBuffer&& frameBuffer, double timestamp, int frameNumber) {
            frameBuffers.emplace_back(std::move(frameBuffer)); // Zero-copy move
        },
        [this](double currentTime, double totalDuration, double progress) {
            emit extractionProgress(progress);
        },
        targetInterval
    );

    if (frameCount < 0) {
        emit extractionError(QString("Frame extraction failed: %1").arg(QString::fromStdString(m_lastDecoder->getLastError())));
        return -1;
    }

    return frameCount;
}

void VideoProcessor::cleanupFrames(const std::vector<std::string>& framePaths)
{
    for (const auto& path : framePaths) {
        QFile::remove(QString::fromStdString(path));
    }
}