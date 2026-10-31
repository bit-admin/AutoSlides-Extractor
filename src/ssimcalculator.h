#ifndef SSIMCALCULATOR_H
#define SSIMCALCULATOR_H

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <thread>
#include <future>
#include <mutex>
#include <memory>
#include <atomic>
#include <QObject>
#include "memoryoptimizer.h"

// Forward declarations
class SSIMMemoryPool;
class OptimizedSSIMCalculator;

/**
 * Memory pool for SSIM operations to reduce allocation overhead
 */
class SSIMMemoryPool {
public:
    SSIMMemoryPool();
    ~SSIMMemoryPool();

    // Acquire buffers for SSIM calculations
    cv::Mat acquireGrayBuffer(int width, int height);
    cv::Mat acquireWorkBuffer(int width, int height, int type = CV_64F);

    // Release buffers back to the pool
    void releaseBuffer(cv::Mat& buffer);

    // Clear all buffers (useful for memory cleanup)
    void clear();

private:
    struct BufferInfo {
        cv::Mat buffer;
        int width;
        int height;
        int type;
        bool inUse;
    };

    std::vector<BufferInfo> m_grayBuffers;
    std::vector<BufferInfo> m_workBuffers;
    std::mutex m_poolMutex;

    // Find or create a buffer with the specified dimensions
    cv::Mat findOrCreateBuffer(std::vector<BufferInfo>& buffers, int width, int height, int type);
};

/**
 * Batch SSIM helper. Statistics are computed with exact integer sums
 * (see SSIMCalculator::globalSSIMFromGray).
 */
class OptimizedSSIMCalculator {
public:
    OptimizedSSIMCalculator();
    ~OptimizedSSIMCalculator();

    // Global SSIM of two 8-bit grayscale images, clamped to [0, 1]
    double calculateOptimizedSSIM(const cv::Mat& gray1, const cv::Mat& gray2);

    // Batch processing with shared downsampling
    std::vector<double> calculateBatchSSIM(const std::vector<cv::Mat>& frames,
                                         bool enableDownsampling = true,
                                         int downsampleWidth = 480,
                                         int downsampleHeight = 270);

    // Set memory pool for buffer reuse
    void setMemoryPool(std::shared_ptr<SSIMMemoryPool> pool);

private:
    // Memory pool for buffer management
    std::shared_ptr<SSIMMemoryPool> m_memoryPool;
};

struct SSIMTask {
    int index;
    std::string img1Path;
    std::string img2Path;
    cv::Mat img1;           // For direct Mat input
    cv::Mat img2;           // For direct Mat input
    bool useMatInput = false; // Flag to indicate whether to use Mat or file paths
    bool enableDownsampling = true; // Flag to enable downsampling
    int downsampleWidth = 480;      // Target width for downsampling
    int downsampleHeight = 270;     // Target height for downsampling
};

struct SSIMResult {
    int index;
    double score;
};

class SSIMCalculator : public QObject
{
    Q_OBJECT

public:
    SSIMCalculator(QObject* parent = nullptr);

    /**
     * Calculate global SSIM between two images
     * @param img1Path Path to first image
     * @param img2Path Path to second image
     * @return SSIM score between 0 and 1
     */
    double calculateGlobalSSIM(const std::string& img1Path, const std::string& img2Path);

    /**
     * Calculate global SSIM between two cv::Mat images
     * @param img1 First image
     * @param img2 Second image
     * @return SSIM score between 0 and 1
     */
    double calculateGlobalSSIM(const cv::Mat& img1, const cv::Mat& img2);

    /**
     * Calculate global SSIM between two cv::Mat images with configurable downsampling
     * @param img1 First image
     * @param img2 Second image
     * @param enableDownsampling Whether to enable downsampling
     * @param downsampleWidth Target width for downsampling
     * @param downsampleHeight Target height for downsampling
     * @return SSIM score between 0 and 1
     */
    double calculateGlobalSSIM(const cv::Mat& img1, const cv::Mat& img2,
                             bool enableDownsampling, int downsampleWidth, int downsampleHeight);

    /**
     * Prepare a frame for SSIM: optional INTER_AREA downsampling, then BGR->gray.
     * Doing this once per frame lets adjacent pairs share the work.
     * @return 8-bit single-channel image that owns its data
     */
    static cv::Mat prepareForSSIM(const cv::Mat& image, bool enableDownsampling,
                                  int downsampleWidth, int downsampleHeight);

    /**
     * Global (single-window) SSIM of two prepared 8-bit grayscale images.
     * Sums are accumulated in exact integer arithmetic, so the result does not
     * depend on CPU architecture or SIMD support.
     */
    static double globalSSIMFromGray(const cv::Mat& gray1, const cv::Mat& gray2);

    /**
     * Calculate SSIM scores for multiple image pairs using multi-threading
     * @param tasks Vector of SSIM tasks with image paths and indices
     * @return Vector of SSIM results with scores and original indices
     */
    std::vector<SSIMResult> calculateMultiThreadedSSIM(const std::vector<SSIMTask>& tasks);

    /**
     * Calculate SSIM scores for multiple frames using optimized batch processing
     * @param frames Vector of frames to process
     * @param enableDownsampling Whether to enable downsampling
     * @param downsampleWidth Target width for downsampling
     * @param downsampleHeight Target height for downsampling
     * @return Vector of SSIM scores between consecutive frames
     */
    std::vector<double> calculateBatchSSIM(const std::vector<cv::Mat>& frames,
                                         bool enableDownsampling = true,
                                         int downsampleWidth = 480,
                                         int downsampleHeight = 270);

    /**
     * Optimized calculate SSIM scores from FrameBuffers using zero-copy operations
     * @param frameBuffers Vector of FrameBuffers to process
     * @param enableDownsampling Whether to enable downsampling
     * @param downsampleWidth Target width for downsampling
     * @param downsampleHeight Target height for downsampling
     * @return Vector of SSIM scores between consecutive frames
     */
    std::vector<double> calculateBatchSSIMFromFrameBuffers(const std::vector<FrameBuffer>& frameBuffers,
                                                          bool enableDownsampling = true,
                                                          int downsampleWidth = 480,
                                                          int downsampleHeight = 270);

signals:
    /**
     * Emitted when multi-threaded SSIM calculation progress updates
     * @param completed Number of completed tasks
     * @param total Total number of tasks
     */
    void calculationProgress(int completed, int total);

private:
    /**
     * Convert image to grayscale using standard luminance formula
     * @param image Input color image
     * @return Grayscale image
     */
    cv::Mat convertToGrayscale(const cv::Mat& image);

    /**
     * Downsample image to specified dimensions to reduce video artifacts
     * @param image Input image
     * @param targetWidth Target width for downsampling
     * @param targetHeight Target height for downsampling
     * @return Downsampled image
     */
    cv::Mat downsampleImage(const cv::Mat& image, int targetWidth, int targetHeight);

    /**
     * Get optimal number of threads for SSIM calculation
     * @return Number of threads (CPU cores - 1, minimum 1)
     */
    int getOptimalThreadCount() const;

    /**
     * Process a batch of SSIM tasks in a single thread
     * @param tasks Vector of tasks to process
     * @param startIndex Starting index in the tasks vector
     * @param endIndex Ending index in the tasks vector
     * @param results Reference to results vector to store results
     * @param completedCount Reference to atomic counter for progress tracking
     * @param totalTasks Total number of tasks for progress calculation
     * @param progressInterval Interval for progress reporting
     */
    void processBatch(const std::vector<SSIMTask>& tasks,
                     int startIndex,
                     int endIndex,
                     std::vector<SSIMResult>& results,
                     std::atomic<int>& completedCount,
                     int totalTasks,
                     int progressInterval);

    // SSIM constants
    static constexpr double C1 = 6.5025;   // (0.01 * 255)^2
    static constexpr double C2 = 58.5225;  // (0.03 * 255)^2

    // Optimized calculator and memory pool instances
    std::unique_ptr<OptimizedSSIMCalculator> m_optimizedCalculator;
    std::shared_ptr<SSIMMemoryPool> m_memoryPool;
};

#endif // SSIMCALCULATOR_H