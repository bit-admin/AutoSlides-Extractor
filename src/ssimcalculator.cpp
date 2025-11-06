#include "ssimcalculator.h"
#include "imageiohelper.h"
#include "memoryoptimizer.h"
#include <iostream>
#include <algorithm>
#include <atomic>
#include <QTimer>
#include <cstring>

#if SIMD_AVAILABLE
    #ifdef __APPLE__
        #include <Accelerate/Accelerate.h>
    #elif defined(__x86_64__) || defined(_M_X64)
        #include <immintrin.h>
    #endif
#endif

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

OptimizedSSIMCalculator::OptimizedSSIMCalculator() {
    // Pre-allocate working buffers for common sizes
    m_workBuffer1 = cv::Mat::zeros(270, 480, CV_64F);
    m_workBuffer2 = cv::Mat::zeros(270, 480, CV_64F);
    m_grayBuffer1 = cv::Mat::zeros(270, 480, CV_8UC1);
    m_grayBuffer2 = cv::Mat::zeros(270, 480, CV_8UC1);
}

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

    // Calculate means using SIMD if available, with error handling
    double mean1, mean2, var1, var2, covariance;

    try {
#if SIMD_AVAILABLE
        // Use SIMD optimizations for grayscale images
        if (gray1.type() == CV_8UC1 && gray2.type() == CV_8UC1) {
            mean1 = calculateMeanSIMD(gray1);
            mean2 = calculateMeanSIMD(gray2);
            var1 = calculateVarianceSIMD(gray1, mean1);
            var2 = calculateVarianceSIMD(gray2, mean2);
            covariance = calculateCovarianceSIMD(gray1, gray2, mean1, mean2);
        } else {
            // Fall back to non-SIMD for non-grayscale images
            mean1 = calculateMeanFallback(gray1);
            mean2 = calculateMeanFallback(gray2);
            var1 = calculateVarianceFallback(gray1, mean1);
            var2 = calculateVarianceFallback(gray2, mean2);
            covariance = calculateCovarianceFallback(gray1, gray2, mean1, mean2);
        }
#else
        mean1 = calculateMeanFallback(gray1);
        mean2 = calculateMeanFallback(gray2);
        var1 = calculateVarianceFallback(gray1, mean1);
        var2 = calculateVarianceFallback(gray2, mean2);
        covariance = calculateCovarianceFallback(gray1, gray2, mean1, mean2);
#endif
    } catch (const std::exception& e) {
        std::cerr << "Error in SIMD calculations, falling back to standard implementation: " << e.what() << std::endl;
        mean1 = calculateMeanFallback(gray1);
        mean2 = calculateMeanFallback(gray2);
        var1 = calculateVarianceFallback(gray1, mean1);
        var2 = calculateVarianceFallback(gray2, mean2);
        covariance = calculateCovarianceFallback(gray1, gray2, mean1, mean2);
    }

    // Validate calculated values
    if (std::isnan(mean1) || std::isnan(mean2) || std::isnan(var1) || std::isnan(var2) || std::isnan(covariance)) {
        std::cerr << "Warning: NaN values detected in SSIM calculation" << std::endl;
        return 0.0;
    }

    // Calculate SSIM using the global formula
    double numerator = (2 * mean1 * mean2 + C1) * (2 * covariance + C2);
    double denominator = (mean1 * mean1 + mean2 * mean2 + C1) * (var1 + var2 + C2);

    if (denominator == 0.0) {
        return 1.0; // Perfect similarity when both images are uniform
    }

    double result = numerator / denominator;

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

#if SIMD_AVAILABLE

#ifdef __APPLE__
// Apple Silicon NEON optimizations using Accelerate framework
double OptimizedSSIMCalculator::calculateMeanSIMD(const cv::Mat& grayImage) {
    if (grayImage.type() != CV_8UC1) {
        return calculateMeanFallback(grayImage);
    }

    const uint8_t* data = grayImage.ptr<uint8_t>();
    const int totalPixels = grayImage.rows * grayImage.cols;

    // Use vDSP for efficient sum calculation
    float sum = 0.0f;
    vDSP_Length length = static_cast<vDSP_Length>(totalPixels);

    // Convert uint8 to float and sum
    std::vector<float> floatData(totalPixels);
    vDSP_vfltu8(data, 1, floatData.data(), 1, length);
    vDSP_sve(floatData.data(), 1, &sum, length);

    return static_cast<double>(sum) / totalPixels;
}

double OptimizedSSIMCalculator::calculateVarianceSIMD(const cv::Mat& grayImage, double mean) {
    if (grayImage.type() != CV_8UC1) {
        return calculateVarianceFallback(grayImage, mean);
    }

    const uint8_t* data = grayImage.ptr<uint8_t>();
    const int totalPixels = grayImage.rows * grayImage.cols;

    // Convert to float and subtract mean
    std::vector<float> floatData(totalPixels);
    std::vector<float> diff(totalPixels);

    vDSP_Length length = static_cast<vDSP_Length>(totalPixels);
    vDSP_vfltu8(data, 1, floatData.data(), 1, length);

    float meanFloat = static_cast<float>(-mean);  // Negative for subtraction
    vDSP_vsadd(floatData.data(), 1, &meanFloat, diff.data(), 1, length);

    // Square the differences
    vDSP_vsq(diff.data(), 1, diff.data(), 1, length);

    // Sum the squared differences
    float variance = 0.0f;
    vDSP_sve(diff.data(), 1, &variance, length);

    return static_cast<double>(variance) / totalPixels;
}

double OptimizedSSIMCalculator::calculateCovarianceSIMD(const cv::Mat& gray1, const cv::Mat& gray2, double mean1, double mean2) {
    if (gray1.type() != CV_8UC1 || gray2.type() != CV_8UC1) {
        return calculateCovarianceFallback(gray1, gray2, mean1, mean2);
    }

    const uint8_t* data1 = gray1.ptr<uint8_t>();
    const uint8_t* data2 = gray2.ptr<uint8_t>();
    const int totalPixels = gray1.rows * gray1.cols;

    // Convert to float
    std::vector<float> floatData1(totalPixels);
    std::vector<float> floatData2(totalPixels);
    std::vector<float> diff1(totalPixels);
    std::vector<float> diff2(totalPixels);

    vDSP_Length length = static_cast<vDSP_Length>(totalPixels);
    vDSP_vfltu8(data1, 1, floatData1.data(), 1, length);
    vDSP_vfltu8(data2, 1, floatData2.data(), 1, length);

    // Subtract means
    float meanFloat1 = static_cast<float>(-mean1);  // Negative for subtraction
    float meanFloat2 = static_cast<float>(-mean2);  // Negative for subtraction
    vDSP_vsadd(floatData1.data(), 1, &meanFloat1, diff1.data(), 1, length);
    vDSP_vsadd(floatData2.data(), 1, &meanFloat2, diff2.data(), 1, length);

    // Multiply differences
    vDSP_vmul(diff1.data(), 1, diff2.data(), 1, diff1.data(), 1, length);

    // Sum the products
    float covariance = 0.0f;
    vDSP_sve(diff1.data(), 1, &covariance, length);

    return static_cast<double>(covariance) / totalPixels;
}

#elif defined(__x86_64__) || defined(_M_X64)
// x86_64 AVX2 optimizations
double OptimizedSSIMCalculator::calculateMeanSIMD(const cv::Mat& grayImage) {
    if (grayImage.type() != CV_8UC1) {
        return calculateMeanFallback(grayImage);
    }

    const uint8_t* data = grayImage.ptr<uint8_t>();
    const int totalPixels = grayImage.rows * grayImage.cols;
    const int simdPixels = (totalPixels / 32) * 32; // Process 32 pixels at a time with AVX2

    __m256i sum = _mm256_setzero_si256();

    // Process 32 pixels at a time
    for (int i = 0; i < simdPixels; i += 32) {
        __m256i pixels = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + i));

        // Convert uint8 to uint16 to avoid overflow
        __m256i pixels_lo = _mm256_unpacklo_epi8(pixels, _mm256_setzero_si256());
        __m256i pixels_hi = _mm256_unpackhi_epi8(pixels, _mm256_setzero_si256());

        // Add to sum
        sum = _mm256_add_epi16(sum, pixels_lo);
        sum = _mm256_add_epi16(sum, pixels_hi);
    }

    // Extract sum from SIMD register
    uint16_t sumArray[16];
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(sumArray), sum);

    uint64_t totalSum = 0;
    for (int i = 0; i < 16; ++i) {
        totalSum += sumArray[i];
    }

    // Process remaining pixels
    for (int i = simdPixels; i < totalPixels; ++i) {
        totalSum += data[i];
    }

    return static_cast<double>(totalSum) / totalPixels;
}

double OptimizedSSIMCalculator::calculateVarianceSIMD(const cv::Mat& grayImage, double mean) {
    if (grayImage.type() != CV_8UC1) {
        return calculateVarianceFallback(grayImage, mean);
    }

    const uint8_t* data = grayImage.ptr<uint8_t>();
    const int totalPixels = grayImage.rows * grayImage.cols;
    const int simdPixels = (totalPixels / 32) * 32;

    __m256 meanVec = _mm256_set1_ps(static_cast<float>(mean));
    __m256 sumVec = _mm256_setzero_ps();

    // Process 8 pixels at a time (converting to float)
    for (int i = 0; i < simdPixels; i += 8) {
        // Load 8 uint8 pixels and convert to float
        __m128i pixels8 = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(data + i));
        __m128i pixels16 = _mm_unpacklo_epi8(pixels8, _mm_setzero_si128());
        __m128i pixels32_lo = _mm_unpacklo_epi16(pixels16, _mm_setzero_si128());
        __m128i pixels32_hi = _mm_unpackhi_epi16(pixels16, _mm_setzero_si128());

        __m256 pixelsFloat = _mm256_cvtepi32_ps(_mm256_set_m128i(pixels32_hi, pixels32_lo));

        // Subtract mean and square
        __m256 diff = _mm256_sub_ps(pixelsFloat, meanVec);
        __m256 squared = _mm256_mul_ps(diff, diff);

        // Add to sum
        sumVec = _mm256_add_ps(sumVec, squared);
    }

    // Extract sum from SIMD register
    float sumArray[8];
    _mm256_storeu_ps(sumArray, sumVec);

    double totalSum = 0.0;
    for (int i = 0; i < 8; ++i) {
        totalSum += sumArray[i];
    }

    // Process remaining pixels
    for (int i = simdPixels; i < totalPixels; ++i) {
        double diff = data[i] - mean;
        totalSum += diff * diff;
    }

    return totalSum / totalPixels;
}

double OptimizedSSIMCalculator::calculateCovarianceSIMD(const cv::Mat& gray1, const cv::Mat& gray2, double mean1, double mean2) {
    if (gray1.type() != CV_8UC1 || gray2.type() != CV_8UC1) {
        return calculateCovarianceFallback(gray1, gray2, mean1, mean2);
    }

    const uint8_t* data1 = gray1.ptr<uint8_t>();
    const uint8_t* data2 = gray2.ptr<uint8_t>();
    const int totalPixels = gray1.rows * gray1.cols;
    const int simdPixels = (totalPixels / 8) * 8;

    __m256 mean1Vec = _mm256_set1_ps(static_cast<float>(mean1));
    __m256 mean2Vec = _mm256_set1_ps(static_cast<float>(mean2));
    __m256 sumVec = _mm256_setzero_ps();

    // Process 8 pixels at a time
    for (int i = 0; i < simdPixels; i += 8) {
        // Load and convert pixels to float
        __m128i pixels1_8 = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(data1 + i));
        __m128i pixels2_8 = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(data2 + i));

        __m128i pixels1_16 = _mm_unpacklo_epi8(pixels1_8, _mm_setzero_si128());
        __m128i pixels2_16 = _mm_unpacklo_epi8(pixels2_8, _mm_setzero_si128());

        __m128i pixels1_32_lo = _mm_unpacklo_epi16(pixels1_16, _mm_setzero_si128());
        __m128i pixels1_32_hi = _mm_unpackhi_epi16(pixels1_16, _mm_setzero_si128());
        __m128i pixels2_32_lo = _mm_unpacklo_epi16(pixels2_16, _mm_setzero_si128());
        __m128i pixels2_32_hi = _mm_unpackhi_epi16(pixels2_16, _mm_setzero_si128());

        __m256 pixels1Float = _mm256_cvtepi32_ps(_mm256_set_m128i(pixels1_32_hi, pixels1_32_lo));
        __m256 pixels2Float = _mm256_cvtepi32_ps(_mm256_set_m128i(pixels2_32_hi, pixels2_32_lo));

        // Subtract means and multiply
        __m256 diff1 = _mm256_sub_ps(pixels1Float, mean1Vec);
        __m256 diff2 = _mm256_sub_ps(pixels2Float, mean2Vec);
        __m256 product = _mm256_mul_ps(diff1, diff2);

        // Add to sum
        sumVec = _mm256_add_ps(sumVec, product);
    }

    // Extract sum from SIMD register
    float sumArray[8];
    _mm256_storeu_ps(sumArray, sumVec);

    double totalSum = 0.0;
    for (int i = 0; i < 8; ++i) {
        totalSum += sumArray[i];
    }

    // Process remaining pixels
    for (int i = simdPixels; i < totalPixels; ++i) {
        double diff1 = data1[i] - mean1;
        double diff2 = data2[i] - mean2;
        totalSum += diff1 * diff2;
    }

    return totalSum / totalPixels;
}

#endif // __x86_64__

#endif // SIMD_AVAILABLE

// Fallback implementations for non-SIMD systems
double OptimizedSSIMCalculator::calculateMeanFallback(const cv::Mat& grayImage) {
    cv::Scalar meanScalar = cv::mean(grayImage);
    return meanScalar[0];
}

double OptimizedSSIMCalculator::calculateVarianceFallback(const cv::Mat& grayImage, double mean) {
    cv::Mat diff;
    grayImage.convertTo(diff, CV_64F);
    diff -= mean;

    cv::Mat variance;
    cv::multiply(diff, diff, variance);

    cv::Scalar varianceScalar = cv::mean(variance);
    return varianceScalar[0];
}

double OptimizedSSIMCalculator::calculateCovarianceFallback(const cv::Mat& gray1, const cv::Mat& gray2, double mean1, double mean2) {
    cv::Mat diff1, diff2;
    gray1.convertTo(diff1, CV_64F);
    gray2.convertTo(diff2, CV_64F);

    diff1 -= mean1;
    diff2 -= mean2;

    cv::Mat covariance;
    cv::multiply(diff1, diff2, covariance);

    cv::Scalar covarianceScalar = cv::mean(covariance);
    return covarianceScalar[0];
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
    cv::Mat processedImg1 = img1;
    cv::Mat processedImg2 = img2;

    // Apply downsampling if enabled
    if (enableDownsampling) {
        processedImg1 = downsampleImage(img1, downsampleWidth, downsampleHeight);
        processedImg2 = downsampleImage(img2, downsampleWidth, downsampleHeight);
    }

    // Convert to grayscale
    cv::Mat gray1 = convertToGrayscale(processedImg1);
    cv::Mat gray2 = convertToGrayscale(processedImg2);

    // Ensure images have the same dimensions
    if (gray1.size() != gray2.size()) {
        cv::resize(gray2, gray2, gray1.size());
    }

    // Calculate means
    double mean1 = calculateMean(gray1);
    double mean2 = calculateMean(gray2);

    // Calculate variances
    double var1 = calculateVariance(gray1, mean1);
    double var2 = calculateVariance(gray2, mean2);

    // Calculate covariance
    double covariance = calculateCovariance(gray1, gray2, mean1, mean2);

    // Calculate SSIM using the global formula
    double numerator = (2 * mean1 * mean2 + C1) * (2 * covariance + C2);
    double denominator = (mean1 * mean1 + mean2 * mean2 + C1) * (var1 + var2 + C2);

    if (denominator == 0.0) {
        return 1.0; // Perfect similarity when both images are uniform
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

double SSIMCalculator::calculateMean(const cv::Mat& grayImage)
{
    // Use optimized calculator if available and image is grayscale
    if (m_optimizedCalculator && grayImage.type() == CV_8UC1) {
#if SIMD_AVAILABLE
        return m_optimizedCalculator->calculateMeanSIMD(grayImage);
#else
        return m_optimizedCalculator->calculateMeanFallback(grayImage);
#endif
    }

    // Fallback to original implementation for other cases
    cv::Scalar meanScalar = cv::mean(grayImage);
    return meanScalar[0];
}

double SSIMCalculator::calculateVariance(const cv::Mat& grayImage, double mean)
{
    // Use optimized calculator if available and image is grayscale
    if (m_optimizedCalculator && grayImage.type() == CV_8UC1) {
#if SIMD_AVAILABLE
        return m_optimizedCalculator->calculateVarianceSIMD(grayImage, mean);
#else
        return m_optimizedCalculator->calculateVarianceFallback(grayImage, mean);
#endif
    }

    // Fallback to original implementation for other cases
    cv::Mat diff;
    grayImage.convertTo(diff, CV_64F);
    diff -= mean;

    cv::Mat variance;
    cv::multiply(diff, diff, variance);

    cv::Scalar varianceScalar = cv::mean(variance);
    return varianceScalar[0];
}

double SSIMCalculator::calculateCovariance(const cv::Mat& gray1, const cv::Mat& gray2, double mean1, double mean2)
{
    // Use optimized calculator if available and images are grayscale
    if (m_optimizedCalculator && gray1.type() == CV_8UC1 && gray2.type() == CV_8UC1) {
#if SIMD_AVAILABLE
        return m_optimizedCalculator->calculateCovarianceSIMD(gray1, gray2, mean1, mean2);
#else
        return m_optimizedCalculator->calculateCovarianceFallback(gray1, gray2, mean1, mean2);
#endif
    }

    // Fallback to original implementation for other cases
    cv::Mat diff1, diff2;
    gray1.convertTo(diff1, CV_64F);
    gray2.convertTo(diff2, CV_64F);

    diff1 -= mean1;
    diff2 -= mean2;

    cv::Mat covariance;
    cv::multiply(diff1, diff2, covariance);

    cv::Scalar covarianceScalar = cv::mean(covariance);
    return covarianceScalar[0];
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