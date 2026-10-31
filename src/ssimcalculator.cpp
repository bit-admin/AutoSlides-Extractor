#include "ssimcalculator.h"
#include "imageiohelper.h"
#include "memoryoptimizer.h"
#include <iostream>
#include <algorithm>
#include <atomic>
#include <QTimer>
#include <cstring>

#include <cstdint>

// ============================================================================
// SSIMMemoryPool Implementation
// ============================================================================

SSIMMemoryPool::SSIMMemoryPool() {
    // Pre-allocate some common buffer sizes
    m_grayBuffers.reserve(8);
    m_workBuffers.reserve(16);
}

SSIMMemoryPool::~SSIMMemoryPool() {
    clear();
}

cv::Mat SSIMMemoryPool::acquireGrayBuffer(int width, int height) {
    return findOrCreateBuffer(m_grayBuffers, width, height, CV_8UC1);
}

cv::Mat SSIMMemoryPool::acquireWorkBuffer(int width, int height, int type) {
    return findOrCreateBuffer(m_workBuffers, width, height, type);
}

void SSIMMemoryPool::releaseBuffer(cv::Mat& buffer) {
    if (buffer.empty()) return;

    std::lock_guard<std::mutex> lock(m_poolMutex);

    // Find the buffer in our pools and mark it as available
    auto markAvailable = [&buffer](std::vector<BufferInfo>& buffers) {
        for (auto& bufferInfo : buffers) {
            if (bufferInfo.buffer.data == buffer.data) {
                bufferInfo.inUse = false;
                return true;
            }
        }
        return false;
    };

    if (!markAvailable(m_grayBuffers)) {
        markAvailable(m_workBuffers);
    }

    // Clear the reference to prevent accidental use
    buffer = cv::Mat();
}

void SSIMMemoryPool::clear() {
    std::lock_guard<std::mutex> lock(m_poolMutex);
    m_grayBuffers.clear();
    m_workBuffers.clear();
}

cv::Mat SSIMMemoryPool::findOrCreateBuffer(std::vector<BufferInfo>& buffers, int width, int height, int type) {
    std::lock_guard<std::mutex> lock(m_poolMutex);

    // Try to find an available buffer with matching dimensions
    for (auto& bufferInfo : buffers) {
        if (!bufferInfo.inUse &&
            bufferInfo.width == width &&
            bufferInfo.height == height &&
            bufferInfo.type == type) {
            bufferInfo.inUse = true;
            return bufferInfo.buffer;
        }
    }

    // Create a new buffer if none available
    BufferInfo newBuffer;
    newBuffer.width = width;
    newBuffer.height = height;
    newBuffer.type = type;
    newBuffer.inUse = true;
    newBuffer.buffer = cv::Mat::zeros(height, width, type);

    buffers.push_back(newBuffer);
    return newBuffer.buffer;
}

// ============================================================================
// OptimizedSSIMCalculator Implementation
// ============================================================================

OptimizedSSIMCalculator::OptimizedSSIMCalculator() = default;

OptimizedSSIMCalculator::~OptimizedSSIMCalculator() {
    // Buffers will be automatically released
}

void OptimizedSSIMCalculator::setMemoryPool(std::shared_ptr<SSIMMemoryPool> pool) {
    m_memoryPool = pool;
}

double OptimizedSSIMCalculator::calculateOptimizedSSIM(const cv::Mat& gray1, const cv::Mat& gray2) {
    // Ensure images have the same dimensions
    if (gray1.size() != gray2.size()) {
        std::cerr << "Warning: Images have different dimensions in OptimizedSSIMCalculator" << std::endl;
        return 0.0;
    }

    // Ensure images are not empty
    if (gray1.empty() || gray2.empty()) {
        std::cerr << "Warning: Empty images in OptimizedSSIMCalculator" << std::endl;
        return 0.0;
    }

    double result = SSIMCalculator::globalSSIMFromGray(gray1, gray2);

    // Clamp result to valid SSIM range [0, 1]
    if (result < 0.0) result = 0.0;
    if (result > 1.0) result = 1.0;

    return result;
}

std::vector<double> OptimizedSSIMCalculator::calculateBatchSSIM(const std::vector<cv::Mat>& frames,
                                                              bool enableDownsampling,
                                                              int downsampleWidth,
                                                              int downsampleHeight) {
    std::vector<double> results;
    if (frames.size() < 2) {
        return results;
    }

    try {
        // Pre-process all frames (convert to grayscale and downsample if needed)
        std::vector<cv::Mat> processedFrames;
        processedFrames.reserve(frames.size());

        for (const auto& frame : frames) {
            if (frame.empty()) {
                std::cerr << "Warning: Empty frame encountered in batch processing" << std::endl;
                continue;
            }

            cv::Mat processed = frame;

            // Apply downsampling if enabled
            if (enableDownsampling && (frame.cols > downsampleWidth || frame.rows > downsampleHeight)) {
                cv::resize(frame, processed, cv::Size(downsampleWidth, downsampleHeight), 0, 0, cv::INTER_AREA);
            }

            // Convert to grayscale if needed
            cv::Mat gray;
            if (processed.channels() == 3) {
                cv::cvtColor(processed, gray, cv::COLOR_BGR2GRAY);
            } else if (processed.channels() == 1) {
                gray = processed; // No need to clone, processed is already a copy
            } else {
                std::cerr << "Warning: Unsupported number of channels: " << processed.channels() << std::endl;
                continue;
            }

            processedFrames.push_back(gray);
        }

        // Ensure we still have enough frames after filtering
        if (processedFrames.size() < 2) {
            std::cerr << "Warning: Not enough valid frames for batch SSIM calculation" << std::endl;
            return results;
        }

        // Calculate SSIM between consecutive frames
        results.reserve(processedFrames.size() - 1);
        for (size_t i = 0; i < processedFrames.size() - 1; ++i) {
            double ssim = calculateOptimizedSSIM(processedFrames[i], processedFrames[i + 1]);
            results.push_back(ssim);
        }

    } catch (const std::exception& e) {
        std::cerr << "Error in batch SSIM calculation: " << e.what() << std::endl;
        results.clear();
    }

    return results;
}

// ============================================================================
// SSIMCalculator Implementation (Updated)
// ============================================================================

SSIMCalculator::SSIMCalculator(QObject* parent) : QObject(parent)
{
    // Initialize optimized calculator and memory pool
    m_memoryPool = std::make_shared<SSIMMemoryPool>();
    m_optimizedCalculator = std::make_unique<OptimizedSSIMCalculator>();
    m_optimizedCalculator->setMemoryPool(m_memoryPool);
}

double SSIMCalculator::calculateGlobalSSIM(const std::string& img1Path, const std::string& img2Path)
{
    // Use Unicode-safe helper for image loading
    cv::Mat img1 = ImageIOHelper::imreadUnicode(img1Path, cv::IMREAD_COLOR);
    cv::Mat img2 = ImageIOHelper::imreadUnicode(img2Path, cv::IMREAD_COLOR);

    if (img1.empty() || img2.empty()) {
        std::cerr << "Error: Could not load images from " << img1Path << " or " << img2Path << std::endl;
        return 0.0;
    }

    // Ensure images have the same dimensions
    if (img1.size() != img2.size()) {
        cv::resize(img2, img2, img1.size());
    }

    return calculateGlobalSSIM(img1, img2);
}

double SSIMCalculator::calculateGlobalSSIM(const cv::Mat& img1, const cv::Mat& img2)
{
    return calculateGlobalSSIM(img1, img2, true, 480, 270);
}

double SSIMCalculator::calculateGlobalSSIM(const cv::Mat& img1, const cv::Mat& img2,
                                         bool enableDownsampling, int downsampleWidth, int downsampleHeight)
{
    return globalSSIMFromGray(prepareForSSIM(img1, enableDownsampling, downsampleWidth, downsampleHeight),
                              prepareForSSIM(img2, enableDownsampling, downsampleWidth, downsampleHeight));
}

cv::Mat SSIMCalculator::prepareForSSIM(const cv::Mat& image, bool enableDownsampling,
                                       int downsampleWidth, int downsampleHeight)
{
    if (image.empty()) {
        return cv::Mat();
    }

    // Same order as before: downsample the colour image, then convert to gray.
    cv::Mat processed = image;
    if (enableDownsampling && downsampleWidth > 0 && downsampleHeight > 0 &&
        (image.cols > downsampleWidth || image.rows > downsampleHeight)) {
        cv::resize(image, processed, cv::Size(downsampleWidth, downsampleHeight), 0, 0, cv::INTER_AREA);
    }

    cv::Mat gray;
    if (processed.channels() == 3) {
        cv::cvtColor(processed, gray, cv::COLOR_BGR2GRAY);
    } else if (processed.channels() == 4) {
        cv::cvtColor(processed, gray, cv::COLOR_BGRA2GRAY);
    } else if (processed.channels() == 1) {
        gray = processed.clone(); // own the data: callers may keep it across chunks
    } else {
        return cv::Mat();
    }

    if (gray.depth() != CV_8U) {
        gray.convertTo(gray, CV_8U);
    }
    return gray;
}

double SSIMCalculator::globalSSIMFromGray(const cv::Mat& gray1, const cv::Mat& gray2)
{
    if (gray1.empty() || gray2.empty() || gray1.type() != CV_8UC1 || gray2.type() != CV_8UC1) {
        return 0.0;
    }

    cv::Mat other = gray2;
    if (other.size() != gray1.size()) {
        cv::resize(gray2, other, gray1.size(), 0, 0, cv::INTER_AREA);
    }

    // Exact integer sums. Per-block uint32 accumulators cannot overflow
    // (4096 * 255 * 255 < 2^32); totals go into uint64.
    constexpr int kBlock = 4096;
    uint64_t s1 = 0, s2 = 0, s11 = 0, s22 = 0, s12 = 0;
    for (int y = 0; y < gray1.rows; ++y) {
        const uint8_t* a = gray1.ptr<uint8_t>(y);
        const uint8_t* b = other.ptr<uint8_t>(y);
        for (int x0 = 0; x0 < gray1.cols; x0 += kBlock) {
            const int x1 = std::min(gray1.cols, x0 + kBlock);
            uint32_t b1 = 0, b2 = 0, b11 = 0, b22 = 0, b12 = 0;
            for (int x = x0; x < x1; ++x) {
                const uint32_t p = a[x], q = b[x];
                b1 += p;
                b2 += q;
                b11 += p * p;
                b22 += q * q;
                b12 += p * q;
            }
            s1 += b1; s2 += b2; s11 += b11; s22 += b22; s12 += b12;
        }
    }

    const double n = static_cast<double>(gray1.total());
    const double mean1 = s1 / n;
    const double mean2 = s2 / n;
    const double var1 = s11 / n - mean1 * mean1;
    const double var2 = s22 / n - mean2 * mean2;
    const double covariance = s12 / n - mean1 * mean2;

    const double numerator = (2 * mean1 * mean2 + C1) * (2 * covariance + C2);
    const double denominator = (mean1 * mean1 + mean2 * mean2 + C1) * (var1 + var2 + C2);
    if (denominator == 0.0) {
        return 1.0;
    }
    return numerator / denominator;
}

cv::Mat SSIMCalculator::convertToGrayscale(const cv::Mat& image)
{
    cv::Mat grayImage;

    if (image.channels() == 3) {
        // Convert BGR to grayscale using standard luminance formula: 0.299*R + 0.587*G + 0.114*B
        // Note: OpenCV uses BGR format, so we need to adjust the formula
        cv::cvtColor(image, grayImage, cv::COLOR_BGR2GRAY);
    } else if (image.channels() == 1) {
        grayImage = image; // No need to clone for read-only operations
    } else {
        std::cerr << "Error: Unsupported number of channels: " << image.channels() << std::endl;
        return cv::Mat();
    }

    return grayImage;
}

cv::Mat SSIMCalculator::downsampleImage(const cv::Mat& image, int targetWidth, int targetHeight)
{
    if (image.empty()) {
        return cv::Mat();
    }

    // If the image is already smaller than or equal to target size, return as is
    if (image.cols <= targetWidth && image.rows <= targetHeight) {
        return image; // No need to clone, caller should handle data persistence if needed
    }

    cv::Mat downsampledImage;
    cv::resize(image, downsampledImage, cv::Size(targetWidth, targetHeight), 0, 0, cv::INTER_AREA);

    return downsampledImage;
}

std::vector<SSIMResult> SSIMCalculator::calculateMultiThreadedSSIM(const std::vector<SSIMTask>& tasks)
{
    if (tasks.empty()) {
        return {};
    }

    const int numThreads = getOptimalThreadCount();
    const int totalTasks = static_cast<int>(tasks.size());
    const int tasksPerThread = (totalTasks + numThreads - 1) / numThreads; // Ceiling division

    std::vector<SSIMResult> results(totalTasks);
    std::vector<std::thread> threads;
    std::atomic<int> completedCount(0);

    // Calculate progress reporting interval (report every ~5% or at least every 10 tasks)
    const int progressInterval = std::max(1, std::min(totalTasks / 20, 10));

    // Create threads and distribute tasks
    for (int i = 0; i < numThreads; ++i) {
        int startIndex = i * tasksPerThread;
        int endIndex = std::min(startIndex + tasksPerThread, totalTasks);

        if (startIndex >= totalTasks) {
            break; // No more tasks for this thread
        }

        threads.emplace_back([this, &tasks, startIndex, endIndex, &results, &completedCount, totalTasks, progressInterval]() {
            processBatch(tasks, startIndex, endIndex, results, completedCount, totalTasks, progressInterval);
        });
    }

    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }

    // Emit final progress
    emit calculationProgress(totalTasks, totalTasks);

    return results;
}

std::vector<double> SSIMCalculator::calculateBatchSSIM(const std::vector<cv::Mat>& frames,
                                                    bool enableDownsampling,
                                                    int downsampleWidth,
                                                    int downsampleHeight)
{
    // Use optimized batch processing if available
    if (m_optimizedCalculator) {
        return m_optimizedCalculator->calculateBatchSSIM(frames, enableDownsampling, downsampleWidth, downsampleHeight);
    }

    // Fallback to sequential processing
    std::vector<double> results;
    if (frames.size() < 2) {
        return results;
    }

    results.reserve(frames.size() - 1);
    for (size_t i = 0; i < frames.size() - 1; ++i) {
        double ssim = calculateGlobalSSIM(frames[i], frames[i + 1], enableDownsampling, downsampleWidth, downsampleHeight);
        results.push_back(ssim);
    }

    return results;
}

int SSIMCalculator::getOptimalThreadCount() const
{
    const int hardwareConcurrency = std::thread::hardware_concurrency();
    // Use hardware_concurrency - 1, but ensure at least 1 thread
    return std::max(1, hardwareConcurrency - 1);
}

void SSIMCalculator::processBatch(const std::vector<SSIMTask>& tasks,
                                 int startIndex,
                                 int endIndex,
                                 std::vector<SSIMResult>& results,
                                 std::atomic<int>& completedCount,
                                 int totalTasks,
                                 int progressInterval)
{
    for (int i = startIndex; i < endIndex; ++i) {
        const SSIMTask& task = tasks[i];

        // Calculate SSIM score for this task
        double score;
        if (task.useMatInput) {
            score = calculateGlobalSSIM(task.img1, task.img2,
                                      task.enableDownsampling,
                                      task.downsampleWidth,
                                      task.downsampleHeight);
        } else {
            // For file-based input, load images using Unicode-safe helper
            cv::Mat img1 = ImageIOHelper::imreadUnicode(task.img1Path, cv::IMREAD_COLOR);
            cv::Mat img2 = ImageIOHelper::imreadUnicode(task.img2Path, cv::IMREAD_COLOR);

            if (img1.empty() || img2.empty()) {
                score = 0.0;
            } else {
                // Ensure images have the same dimensions
                if (img1.size() != img2.size()) {
                    cv::resize(img2, img2, img1.size());
                }

                score = calculateGlobalSSIM(img1, img2,
                                          task.enableDownsampling,
                                          task.downsampleWidth,
                                          task.downsampleHeight);
            }
        }

        // Store result with original index to maintain order
        results[task.index] = {task.index, score};

        // Update progress counter and emit progress signal if needed
        int currentCompleted = completedCount.fetch_add(1) + 1;

        // Emit progress signal at intervals to avoid too frequent updates
        if (currentCompleted % progressInterval == 0 || currentCompleted == totalTasks) {
            emit calculationProgress(currentCompleted, totalTasks);
        }
    }
}

std::vector<double> SSIMCalculator::calculateBatchSSIMFromFrameBuffers(const std::vector<FrameBuffer>& frameBuffers,
                                                                      bool enableDownsampling,
                                                                      int downsampleWidth,
                                                                      int downsampleHeight)
{
    std::vector<double> scores;
    if (frameBuffers.size() < 2) {
        return scores;
    }

    scores.reserve(frameBuffers.size() - 1);

    // Use the shared memory pool for efficient Mat management
    auto memoryPool = std::make_shared<MatMemoryPool>(100);

    for (size_t i = 0; i < frameBuffers.size() - 1; ++i) {
        emit calculationProgress(static_cast<int>(i), static_cast<int>(frameBuffers.size() - 1));

        if (!frameBuffers[i].isValid() || !frameBuffers[i + 1].isValid()) {
            scores.push_back(0.0); // Invalid frames get 0 similarity
            continue;
        }

        // Get zero-copy Mat views
        cv::Mat frame1 = frameBuffers[i].getConstMatView();
        cv::Mat frame2 = frameBuffers[i + 1].getConstMatView();

        if (frame1.empty() || frame2.empty()) {
            scores.push_back(0.0);
            continue;
        }

        // Calculate SSIM with memory pool optimization
        double ssim = 0.0;
        if (enableDownsampling) {
            // Use pooled Mats for downsampling to avoid allocations
            auto pooledMat1 = PooledMat(memoryPool->acquireWorkBuffer(downsampleWidth, downsampleHeight, frame1.type()), memoryPool.get(), false);
            auto pooledMat2 = PooledMat(memoryPool->acquireWorkBuffer(downsampleWidth, downsampleHeight, frame2.type()), memoryPool.get(), false);

            // Downsample frames
            cv::resize(frame1, pooledMat1.get(), cv::Size(downsampleWidth, downsampleHeight), 0, 0, cv::INTER_AREA);
            cv::resize(frame2, pooledMat2.get(), cv::Size(downsampleWidth, downsampleHeight), 0, 0, cv::INTER_AREA);

            // Calculate SSIM on downsampled frames
            ssim = calculateGlobalSSIM(pooledMat1.get(), pooledMat2.get());
        } else {
            // Calculate SSIM directly on original frames (zero-copy)
            ssim = calculateGlobalSSIM(frame1, frame2);
        }

        scores.push_back(ssim);
    }

    return scores;
}