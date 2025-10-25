#ifndef SLIDEDETECTOR_H
#define SLIDEDETECTOR_H

#include <vector>
#include <string>
#include <QObject>
#include <opencv2/opencv.hpp>
#include "ssimcalculator.h"
#include "chunkprocessor.h"
#include "memoryoptimizer.h"

struct SlideDetectionResult {
    std::vector<std::string> selectedSlides;      // For file-based processing
    std::vector<int> selectedSlideIndices;        // For memory-based processing
    std::vector<double> ssimScores;
    int totalFramesProcessed;
    double processingTimeSeconds;
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
     * Detect slides from in-memory frames using the two-stage algorithm with configurable downsampling
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
     * Detect slides from a chunk of frames using the two-stage algorithm with state continuation
     * This method supports chunk-based processing for memory optimization
     * @param newFrames Vector of new frames for this chunk
     * @param state Processing state that maintains continuity across chunks
     * @param isLastChunk Whether this is the final chunk in the sequence
     * @param ssimThreshold SSIM threshold for similarity detection
     * @param verificationCount Number of consecutive frames needed for stability verification
     * @param enableDownsampling Whether to enable downsampling
     * @param downsampleWidth Target width for downsampling
     * @param downsampleHeight Target height for downsampling
     * @return SlideDetectionResult containing selected slide indices for this chunk
     */
    SlideDetectionResult detectSlidesFromChunk(const std::vector<cv::Mat>& newFrames,
                                             ProcessingState& state,
                                             bool isLastChunk,
                                             double ssimThreshold,
                                             int verificationCount,
                                             bool enableDownsampling,
                                             int downsampleWidth,
                                             int downsampleHeight);

    /**
     * Optimized detect slides from FrameBuffer chunk using zero-copy operations
     * @param frameBuffers Vector of FrameBuffers for this chunk
     * @param state Processing state that maintains continuity across chunks
     * @param isLastChunk Whether this is the final chunk in the sequence
     * @param ssimThreshold SSIM threshold for similarity detection
     * @param verificationCount Number of consecutive frames needed for stability verification
     * @param enableDownsampling Whether to enable downsampling
     * @param downsampleWidth Target width for downsampling
     * @param downsampleHeight Target height for downsampling
     * @return SlideDetectionResult containing selected slide indices for this chunk
     */
    SlideDetectionResult detectSlidesFromFrameBufferChunk(const std::vector<FrameBuffer>& frameBuffers,
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

    /**
     * Optimized calculate SSIM scores from FrameBuffers using zero-copy operations
     * @param frameBuffers Vector of FrameBuffers
     * @param enableDownsampling Whether to enable downsampling
     * @param downsampleWidth Target width for downsampling
     * @param downsampleHeight Target height for downsampling
     * @return Vector of SSIM scores between adjacent frames
     */
    std::vector<double> calculateSSIMScoresFromFrameBuffers(const std::vector<FrameBuffer>& frameBuffers,
                                                           bool enableDownsampling,
                                                           int downsampleWidth,
                                                           int downsampleHeight);

signals:
    void ssimCalculationProgress(int current, int total);
    void slideDetectionProgress(int current, int total);
    void detectionError(const QString& error);

private:
    /**
     * Extract slides using the two-stage algorithm
     * @param framePaths Vector of frame file paths
     * @param scores Vector of SSIM scores between adjacent frames
     * @param ssimThreshold SSIM threshold for similarity detection
     * @param verificationCount Number of consecutive frames needed for stability
     * @return Vector of selected slide indices
     */
    std::vector<int> extractSlides(const std::vector<std::string>& framePaths,
                                 const std::vector<double>& scores,
                                 double ssimThreshold,
                                 int verificationCount);

    /**
     * Extract slides using the two-stage algorithm from frame count
     * @param frameCount Total number of frames
     * @param scores Vector of SSIM scores between adjacent frames
     * @param ssimThreshold SSIM threshold for similarity detection
     * @param verificationCount Number of consecutive frames needed for stability
     * @return Vector of selected slide indices
     */
    std::vector<int> extractSlidesFromCount(int frameCount,
                                          const std::vector<double>& scores,
                                          double ssimThreshold,
                                          int verificationCount);

    /**
     * Handle end-of-sequence logic for slide detection
     * @param framePaths Vector of frame file paths
     * @param scores Vector of SSIM scores
     * @param lastStableIndex Index of the last stable frame
     * @param savedSlideIndices Vector of already selected slide indices
     * @param ssimThreshold SSIM threshold
     */
    void handleEndOfSequence(const std::vector<std::string>& framePaths,
                           const std::vector<double>& scores,
                           int lastStableIndex,
                           std::vector<int>& savedSlideIndices,
                           double ssimThreshold);

    /**
     * Handle end-of-sequence logic for slide detection (frame count version)
     * @param frameCount Total number of frames
     * @param scores Vector of SSIM scores
     * @param lastStableIndex Index of the last stable frame
     * @param savedSlideIndices Vector of already selected slide indices
     * @param ssimThreshold SSIM threshold
     */
    void handleEndOfSequenceFromCount(int frameCount,
                                    const std::vector<double>& scores,
                                    int lastStableIndex,
                                    std::vector<int>& savedSlideIndices,
                                    double ssimThreshold);

    /**
     * Update verification state based on current processing position and verification progress
     * @param state Processing state to update
     * @param currentLocalIndex Current local index in the working frames
     * @param verificationProgress How many verification steps have been completed
     * @param verificationCount Total verification count needed
     */
    void updateVerificationState(ProcessingState& state,
                               int currentLocalIndex,
                               int verificationProgress,
                               int verificationCount);

    /**
     * Update verification state at the end of chunk processing
     * @param state Processing state to update
     * @param finalProcessingIndex Final processing index in the chunk
     * @param totalScores Total number of SSIM scores in the chunk
     * @param verificationCount Total verification count needed
     */
    void updateVerificationStateAtChunkEnd(ProcessingState& state,
                                         int finalProcessingIndex,
                                         int totalScores,
                                         int verificationCount);

    SSIMCalculator m_ssimCalculator;
};

#endif // SLIDEDETECTOR_H