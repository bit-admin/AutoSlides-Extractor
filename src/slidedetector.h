#ifndef SLIDEDETECTOR_H
#define SLIDEDETECTOR_H

#include <vector>
#include <string>
#include <QObject>
#include <opencv2/opencv.hpp>
#include "ssimcalculator.h"
#include "chunkprocessor.h"
#include "memoryoptimizer.h"

/**
 * @brief Media-time bounds for one confirmed (saved) slide in a chunk.
 * changeAt = PTS of transition into the content; confirmedAt = PTS of accepted frame.
 */
struct ConfirmedSlideTiming {
    int globalIndex = -1;
    double changeAt = 0.0;
    double confirmedAt = 0.0;
};

struct SlideDetectionResult {
    std::vector<std::string> selectedSlides;      // For file-based processing
    std::vector<int> selectedSlideIndices;        // For memory-based processing
    // Parallel to selectedSlideIndices for this chunk (confirmed saves only).
    std::vector<ConfirmedSlideTiming> confirmedSlideTimings;
    std::vector<double> ssimScores;
    int totalFramesProcessed = 0;
    double processingTimeSeconds = 0.0;
};

class SlideDetector : public QObject
{
    Q_OBJECT

public:
    explicit SlideDetector(QObject *parent = nullptr);

    /**
     * Detect slides from a sequence of frame images using the two-stage algorithm
     * @param framePaths Vector of frame file paths in chronological order
     * @param ssimThreshold SSIM threshold for similarity detection (default: 0.9985)
     * @param verificationCount Number of consecutive frames needed for stability verification (default: 3)
     * @return SlideDetectionResult containing selected slides and statistics
     */
    SlideDetectionResult detectSlides(const std::vector<std::string>& framePaths,
                                    double ssimThreshold = 0.9985,
                                    int verificationCount = 3);

    /**
     * Detect slides from in-memory frames using the two-stage algorithm
     * @param frames Vector of OpenCV Mat frames in chronological order
     * @param ssimThreshold SSIM threshold for similarity detection (default: 0.9985)
     * @param verificationCount Number of consecutive frames needed for stability verification (default: 3)
     * @return SlideDetectionResult containing selected slide indices and statistics
     */
    SlideDetectionResult detectSlidesFromFrames(const std::vector<cv::Mat>& frames,
                                              double ssimThreshold = 0.9985,
                                              int verificationCount = 3);

    /**
     * Detect slides from in-memory frames using the two-stage algorithm with configurable downsampling.
     * Same as feeding all frames to detectSlidesFromChunk as one (last) chunk.
     * @param frames Vector of OpenCV Mat frames in chronological order
     * @param ssimThreshold SSIM threshold for similarity detection
     * @param verificationCount Number of consecutive frames needed for stability verification
     * @param enableDownsampling Whether to enable downsampling
     * @param downsampleWidth Target width for downsampling
     * @param downsampleHeight Target height for downsampling
     * @return SlideDetectionResult containing selected slide indices and statistics
     */
    SlideDetectionResult detectSlidesFromFramesWithConfig(const std::vector<cv::Mat>& frames,
                                                        double ssimThreshold,
                                                        int verificationCount,
                                                        bool enableDownsampling,
                                                        int downsampleWidth,
                                                        int downsampleHeight);

    /**
     * Detect slides from a chunk of frames using the two-stage algorithm with state continuation.
     * Feeding a video chunk by chunk gives exactly the same slides as one pass over all frames,
     * whatever the chunk size: candidate verification carries across chunk boundaries.
     * @param newFrames Vector of new frames for this chunk
     * @param newTimestamps Media PTS seconds parallel to newFrames (same length preferred)
     * @param state Processing state that maintains continuity across chunks
     * @param isLastChunk Whether this is the final chunk in the sequence
     * @param ssimThreshold SSIM threshold for similarity detection
     * @param verificationCount Number of consecutive frames needed for stability verification
     * @param enableDownsampling Whether to enable downsampling
     * @param downsampleWidth Target width for downsampling
     * @param downsampleHeight Target height for downsampling
     * @return Global indices of slides saved in this chunk (all inside this chunk's frame range),
     *         with confirmedSlideTimings parallel to them
     */
    SlideDetectionResult detectSlidesFromChunk(const std::vector<cv::Mat>& newFrames,
                                             const std::vector<double>& newTimestamps,
                                             ProcessingState& state,
                                             bool isLastChunk,
                                             double ssimThreshold,
                                             int verificationCount,
                                             bool enableDownsampling,
                                             int downsampleWidth,
                                             int downsampleHeight);

    /**
     * Calculate SSIM scores for all adjacent frame pairs
     * @param framePaths Vector of frame file paths
     * @return Vector of SSIM scores between adjacent frames
     */
    std::vector<double> calculateSSIMScores(const std::vector<std::string>& framePaths);

    /**
     * Calculate SSIM scores for all adjacent frame pairs from in-memory frames
     * @param frames Vector of OpenCV Mat frames
     * @return Vector of SSIM scores between adjacent frames
     */
    std::vector<double> calculateSSIMScoresFromFrames(const std::vector<cv::Mat>& frames);

    /**
     * Calculate SSIM scores for all adjacent frame pairs from in-memory frames with configurable downsampling
     * @param frames Vector of OpenCV Mat frames
     * @param enableDownsampling Whether to enable downsampling
     * @param downsampleWidth Target width for downsampling
     * @param downsampleHeight Target height for downsampling
     * @return Vector of SSIM scores between adjacent frames
     */
    std::vector<double> calculateSSIMScoresFromFrames(const std::vector<cv::Mat>& frames,
                                                     bool enableDownsampling,
                                                     int downsampleWidth,
                                                     int downsampleHeight);

signals:
    void ssimCalculationProgress(int current, int total);
    void slideDetectionProgress(int current, int total);
    void detectionError(const QString& error);

private:
    /** Downsample + grayscale every frame once (in parallel). */
    static std::vector<cv::Mat> prepareFrames(const std::vector<cv::Mat>& frames,
                                              bool enableDownsampling,
                                              int downsampleWidth,
                                              int downsampleHeight);

    /** SSIM of each adjacent pair of prepared frames (in parallel). */
    static std::vector<double> scoreAdjacent(const std::vector<const cv::Mat*>& prepared);

    SSIMCalculator m_ssimCalculator;
};

#endif // SLIDEDETECTOR_H