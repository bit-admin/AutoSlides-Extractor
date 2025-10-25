#ifndef VIDEOPROCESSOR_H
#define VIDEOPROCESSOR_H

#include <string>
#include <vector>
#include <memory>
#include <QObject>
#include <QProcess>
#include <QThread>
#include "hardwaredecoder.h"
#include "memoryoptimizer.h"
#include <opencv2/opencv.hpp>

class VideoProcessor : public QObject
{
    Q_OBJECT

public:
    explicit VideoProcessor(QObject *parent = nullptr);
    ~VideoProcessor();

    /**
     * Extract frames from video using intelligent I-frame sampling
     * @param videoPath Path to input video file
     * @param frames Output vector of OpenCV Mat frames
     * @param targetInterval Target interval in seconds (used as reference, default: 2.0)
     * @return Number of frames extracted, -1 on error
     */
    int extractFramesIntelligent(const std::string& videoPath,
                               std::vector<cv::Mat>& frames,
                               double targetInterval = 2.0);

    /**
     * Extract frames from video at specified interval (legacy mode)
     * @param videoPath Path to input video file
     * @param outputDir Directory to save extracted frames (for compatibility)
     * @param intervalSeconds Interval between frames in seconds
     * @return Vector of extracted frame file paths (empty for new implementation)
     */
    std::vector<std::string> extractFrames(const std::string& videoPath,
                                         const std::string& outputDir,
                                         double intervalSeconds = 2.0);

    /**
     * Extract frames directly to memory without saving to disk
     * @param videoPath Path to input video file
     * @param frames Output vector of OpenCV Mat frames
     * @param intervalSeconds Interval between frames in seconds
     * @return Number of frames extracted, -1 on error
     */
    int extractFramesToMemory(const std::string& videoPath,
                            std::vector<cv::Mat>& frames,
                            double intervalSeconds = 2.0);

    /**
     * Optimized extract frames using FrameBuffer (zero-copy)
     * @param videoPath Path to input video file
     * @param frameBuffers Output vector of FrameBuffers
     * @param targetInterval Target interval in seconds (used as reference, default: 2.0)
     * @return Number of frames extracted, -1 on error
     */
    int extractFramesOptimized(const std::string& videoPath,
                             std::vector<FrameBuffer>& frameBuffers,
                             double targetInterval = 2.0);

    /**
     * Get comprehensive video information
     * @param videoPath Path to video file
     * @return VideoInfo structure with detailed video properties
     */
    HardwareDecoder::VideoInfo getVideoInfo(const std::string& videoPath);

    /**
     * Get video duration in seconds
     * @param videoPath Path to video file
     * @return Duration in seconds, -1 if error
     */
    double getVideoDuration(const std::string& videoPath);

    /**
     * Get video frame rate
     * @param videoPath Path to video file
     * @return Frame rate, -1 if error
     */
    double getVideoFrameRate(const std::string& videoPath);

    /**
     * Check if hardware acceleration is available
     * @return true if hardware acceleration is supported
     */
    static bool isHardwareAccelerationAvailable();

    /**
     * Get the hardware acceleration method being used by the last decoder
     * @return Hardware acceleration method name (e.g., "videotoolbox", "d3d11va", "Software")
     */
    std::string getHardwareAccelerationMethod() const;

    /**
     * Clean up temporary frame files
     * @param framePaths Vector of frame file paths to delete
     */
    void cleanupFrames(const std::vector<std::string>& framePaths);

    /**
     * Get optimal thread count for FFmpeg (CPU cores - 1)
     * @return Thread count
     */
    static int getOptimalThreadCount();


signals:
    void frameExtracted(int frameNumber, int totalFrames);
    void extractionProgress(double percentage);
    void extractionError(const QString& error);
    void videoAnalysisComplete(const HardwareDecoder::VideoInfo& info);
    void samplingStrategyDetermined(HardwareDecoder::SamplingStrategy strategy);

private:

    bool m_initialized;
    mutable std::unique_ptr<HardwareDecoder> m_lastDecoder;  // Store last decoder for accessing its properties
};

#endif // VIDEOPROCESSOR_H