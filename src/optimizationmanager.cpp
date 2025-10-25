#include "optimizationmanager.h"
#include <iostream>
#include <algorithm>
#include <random>
#include <numeric>
#include <cmath>
#include <sstream>
#include <iomanip>

// Platform-specific SIMD includes
#ifdef __x86_64__
    #include <immintrin.h>
#elif defined(__aarch64__)
    #include <arm_neon.h>
#endif

// ============================================================================
// CPUSSIMCalculator Implementation
// ============================================================================

CPUSSIMCalculator::CPUSSIMCalculator(SIMDInstructionSet instructionSet)
    : m_instructionSet(instructionSet) {

    // Validate that the requested instruction set is supported
    PlatformDetector& detector = PlatformDetector::getInstance();
    if (!detector.isSIMDSupported(instructionSet) && instructionSet != SIMDInstructionSet::None) {
        m_errorMessage = "Requested SIMD instruction set is not supported on this platform";
        m_isReady = false;
        return;
    }

    m_isReady = true;
}

double CPUSSIMCalculator::calculateSSIM(const cv::Mat& img1, const cv::Mat& img2) {
    if (!m_isReady) {
        return 0.0;
    }

    // Convert to grayscale if needed
    cv::Mat gray1, gray2;
    if (img1.channels() == 3) {
        cv::cvtColor(img1, gray1, cv::COLOR_BGR2GRAY);
    } else {
        gray1 = img1;
    }

    if (img2.channels() == 3) {
        cv::cvtColor(img2, gray2, cv::COLOR_BGR2GRAY);
    } else {
        gray2 = img2;
    }

    // Ensure images are the same size
    if (gray1.size() != gray2.size()) {
        cv::resize(gray2, gray2, gray1.size());
    }

    // Convert to floating point
    cv::Mat float1, float2;
    gray1.convertTo(float1, CV_64F);
    gray2.convertTo(float2, CV_64F);

    // Dispatch to appropriate SIMD implementation
    switch (m_instructionSet) {
        case SIMDInstructionSet::SSE4_2:
            return calculateSSIM_SSE4_2(float1, float2);
        case SIMDInstructionSet::AVX2:
            return calculateSSIM_AVX2(float1, float2);
        case SIMDInstructionSet::AVX512F:
        case SIMDInstructionSet::AVX512BW:
        case SIMDInstructionSet::AVX512VL:
            return calculateSSIM_AVX512(float1, float2);
        case SIMDInstructionSet::NEON:
            return calculateSSIM_NEON(float1, float2);
        default:
            return calculateSSIM_Generic(float1, float2);
    }
}

std::vector<double> CPUSSIMCalculator::calculateBatchSSIM(const std::vector<cv::Mat>& images) {
    std::vector<double> results;
    results.reserve(images.size() - 1);

    for (size_t i = 1; i < images.size(); ++i) {
        results.push_back(calculateSSIM(images[i-1], images[i]));
    }

    return results;
}

OptimizationType CPUSSIMCalculator::getOptimizationType() const {
    switch (m_instructionSet) {
        case SIMDInstructionSet::SSE4_2:
            return OptimizationType::SIMD_SSE4_2;
        case SIMDInstructionSet::AVX2:
            return OptimizationType::SIMD_AVX2;
        case SIMDInstructionSet::AVX512F:
        case SIMDInstructionSet::AVX512BW:
        case SIMDInstructionSet::AVX512VL:
            return OptimizationType::SIMD_AVX512;
        case SIMDInstructionSet::NEON:
            return OptimizationType::SIMD_NEON;
        default:
            return OptimizationType::None;
    }
}

double CPUSSIMCalculator::calculateSSIM_Generic(const cv::Mat& img1, const cv::Mat& img2) {
    // Generic implementation without SIMD optimizations
    cv::Scalar mean1 = cv::mean(img1);
    cv::Scalar mean2 = cv::mean(img2);

    double mu1 = mean1[0];
    double mu2 = mean2[0];

    cv::Mat img1_sq, img2_sq, img1_img2;
    cv::multiply(img1, img1, img1_sq);
    cv::multiply(img2, img2, img2_sq);
    cv::multiply(img1, img2, img1_img2);

    cv::Scalar mean1_sq = cv::mean(img1_sq);
    cv::Scalar mean2_sq = cv::mean(img2_sq);
    cv::Scalar mean1_img2 = cv::mean(img1_img2);

    double sigma1_sq = mean1_sq[0] - mu1 * mu1;
    double sigma2_sq = mean2_sq[0] - mu2 * mu2;
    double sigma12 = mean1_img2[0] - mu1 * mu2;

    const double C1 = 6.5025;   // (0.01 * 255)^2
    const double C2 = 58.5225;  // (0.03 * 255)^2

    double numerator = (2 * mu1 * mu2 + C1) * (2 * sigma12 + C2);
    double denominator = (mu1 * mu1 + mu2 * mu2 + C1) * (sigma1_sq + sigma2_sq + C2);

    return numerator / denominator;
}

double CPUSSIMCalculator::calculateSSIM_SSE4_2(const cv::Mat& img1, const cv::Mat& img2) {
#ifdef __SSE4_2__
    double mean1, mean2, var1, var2;
    calculateMeanVariance_SSE4_2(img1, mean1, var1);
    calculateMeanVariance_SSE4_2(img2, mean2, var2);

    double covar = calculateCovariance_SSE4_2(img1, img2, mean1, mean2);

    const double C1 = 6.5025;   // (0.01 * 255)^2
    const double C2 = 58.5225;  // (0.03 * 255)^2

    double numerator = (2 * mean1 * mean2 + C1) * (2 * covar + C2);
    double denominator = (mean1 * mean1 + mean2 * mean2 + C1) * (var1 + var2 + C2);

    return numerator / denominator;
#else
    return calculateSSIM_Generic(img1, img2);
#endif
}

double CPUSSIMCalculator::calculateSSIM_AVX2(const cv::Mat& img1, const cv::Mat& img2) {
#ifdef __AVX2__
    double mean1, mean2, var1, var2;
    calculateMeanVariance_AVX2(img1, mean1, var1);
    calculateMeanVariance_AVX2(img2, mean2, var2);

    double covar = calculateCovariance_AVX2(img1, img2, mean1, mean2);

    const double C1 = 6.5025;   // (0.01 * 255)^2
    const double C2 = 58.5225;  // (0.03 * 255)^2

    double numerator = (2 * mean1 * mean2 + C1) * (2 * covar + C2);
    double denominator = (mean1 * mean1 + mean2 * mean2 + C1) * (var1 + var2 + C2);

    return numerator / denominator;
#else
    return calculateSSIM_Generic(img1, img2);
#endif
}

double CPUSSIMCalculator::calculateSSIM_AVX512(const cv::Mat& img1, const cv::Mat& img2) {
#ifdef __AVX512F__
    double mean1, mean2, var1, var2;
    calculateMeanVariance_AVX512(img1, mean1, var1);
    calculateMeanVariance_AVX512(img2, mean2, var2);

    double covar = calculateCovariance_AVX512(img1, img2, mean1, mean2);

    const double C1 = 6.5025;   // (0.01 * 255)^2
    const double C2 = 58.5225;  // (0.03 * 255)^2

    double numerator = (2 * mean1 * mean2 + C1) * (2 * covar + C2);
    double denominator = (mean1 * mean1 + mean2 * mean2 + C1) * (var1 + var2 + C2);

    return numerator / denominator;
#else
    return calculateSSIM_Generic(img1, img2);
#endif
}

double CPUSSIMCalculator::calculateSSIM_NEON(const cv::Mat& img1, const cv::Mat& img2) {
#ifdef __aarch64__
    double mean1, mean2, var1, var2;
    calculateMeanVariance_NEON(img1, mean1, var1);
    calculateMeanVariance_NEON(img2, mean2, var2);

    double covar = calculateCovariance_NEON(img1, img2, mean1, mean2);

    const double C1 = 6.5025;   // (0.01 * 255)^2
    const double C2 = 58.5225;  // (0.03 * 255)^2

    double numerator = (2 * mean1 * mean2 + C1) * (2 * covar + C2);
    double denominator = (mean1 * mean1 + mean2 * mean2 + C1) * (var1 + var2 + C2);

    return numerator / denominator;
#else
    return calculateSSIM_Generic(img1, img2);
#endif
}

void CPUSSIMCalculator::calculateMeanVariance_SSE4_2(const cv::Mat& gray, double& mean, double& variance) {
#ifdef __SSE4_2__
    const double* data = reinterpret_cast<const double*>(gray.data);
    int total = gray.rows * gray.cols;

    __m128d sum_vec = _mm_setzero_pd();
    __m128d sum_sq_vec = _mm_setzero_pd();

    int i = 0;
    for (; i <= total - 2; i += 2) {
        __m128d vals = _mm_loadu_pd(&data[i]);
        sum_vec = _mm_add_pd(sum_vec, vals);
        sum_sq_vec = _mm_add_pd(sum_sq_vec, _mm_mul_pd(vals, vals));
    }

    double sum_array[2], sum_sq_array[2];
    _mm_storeu_pd(sum_array, sum_vec);
    _mm_storeu_pd(sum_sq_array, sum_sq_vec);

    double sum = sum_array[0] + sum_array[1];
    double sum_sq = sum_sq_array[0] + sum_sq_array[1];

    // Handle remaining elements
    for (; i < total; ++i) {
        sum += data[i];
        sum_sq += data[i] * data[i];
    }

    mean = sum / total;
    variance = (sum_sq / total) - (mean * mean);
#else
    cv::Scalar mean_scalar = cv::mean(gray);
    mean = mean_scalar[0];

    cv::Mat gray_sq;
    cv::multiply(gray, gray, gray_sq);
    cv::Scalar mean_sq_scalar = cv::mean(gray_sq);
    variance = mean_sq_scalar[0] - mean * mean;
#endif
}

void CPUSSIMCalculator::calculateMeanVariance_AVX2(const cv::Mat& gray, double& mean, double& variance) {
#ifdef __AVX2__
    const double* data = reinterpret_cast<const double*>(gray.data);
    int total = gray.rows * gray.cols;

    __m256d sum_vec = _mm256_setzero_pd();
    __m256d sum_sq_vec = _mm256_setzero_pd();

    int i = 0;
    for (; i <= total - 4; i += 4) {
        __m256d vals = _mm256_loadu_pd(&data[i]);
        sum_vec = _mm256_add_pd(sum_vec, vals);
        sum_sq_vec = _mm256_add_pd(sum_sq_vec, _mm256_mul_pd(vals, vals));
    }

    double sum_array[4], sum_sq_array[4];
    _mm256_storeu_pd(sum_array, sum_vec);
    _mm256_storeu_pd(sum_sq_array, sum_sq_vec);

    double sum = sum_array[0] + sum_array[1] + sum_array[2] + sum_array[3];
    double sum_sq = sum_sq_array[0] + sum_sq_array[1] + sum_sq_array[2] + sum_sq_array[3];

    // Handle remaining elements
    for (; i < total; ++i) {
        sum += data[i];
        sum_sq += data[i] * data[i];
    }

    mean = sum / total;
    variance = (sum_sq / total) - (mean * mean);
#else
    calculateMeanVariance_SSE4_2(gray, mean, variance);
#endif
}

void CPUSSIMCalculator::calculateMeanVariance_AVX512(const cv::Mat& gray, double& mean, double& variance) {
#ifdef __AVX512F__
    const double* data = reinterpret_cast<const double*>(gray.data);
    int total = gray.rows * gray.cols;

    __m512d sum_vec = _mm512_setzero_pd();
    __m512d sum_sq_vec = _mm512_setzero_pd();

    int i = 0;
    for (; i <= total - 8; i += 8) {
        __m512d vals = _mm512_loadu_pd(&data[i]);
        sum_vec = _mm512_add_pd(sum_vec, vals);
        sum_sq_vec = _mm512_add_pd(sum_sq_vec, _mm512_mul_pd(vals, vals));
    }

    double sum = _mm512_reduce_add_pd(sum_vec);
    double sum_sq = _mm512_reduce_add_pd(sum_sq_vec);

    // Handle remaining elements
    for (; i < total; ++i) {
        sum += data[i];
        sum_sq += data[i] * data[i];
    }

    mean = sum / total;
    variance = (sum_sq / total) - (mean * mean);
#else
    calculateMeanVariance_AVX2(gray, mean, variance);
#endif
}

void CPUSSIMCalculator::calculateMeanVariance_NEON(const cv::Mat& gray, double& mean, double& variance) {
#ifdef __aarch64__
    const double* data = reinterpret_cast<const double*>(gray.data);
    int total = gray.rows * gray.cols;

    float64x2_t sum_vec = vdupq_n_f64(0.0);
    float64x2_t sum_sq_vec = vdupq_n_f64(0.0);

    int i = 0;
    for (; i <= total - 2; i += 2) {
        float64x2_t vals = vld1q_f64(&data[i]);
        sum_vec = vaddq_f64(sum_vec, vals);
        sum_sq_vec = vaddq_f64(sum_sq_vec, vmulq_f64(vals, vals));
    }

    double sum = vgetq_lane_f64(sum_vec, 0) + vgetq_lane_f64(sum_vec, 1);
    double sum_sq = vgetq_lane_f64(sum_sq_vec, 0) + vgetq_lane_f64(sum_sq_vec, 1);

    // Handle remaining elements
    for (; i < total; ++i) {
        sum += data[i];
        sum_sq += data[i] * data[i];
    }

    mean = sum / total;
    variance = (sum_sq / total) - (mean * mean);
#else
    cv::Scalar mean_scalar = cv::mean(gray);
    mean = mean_scalar[0];

    cv::Mat gray_sq;
    cv::multiply(gray, gray, gray_sq);
    cv::Scalar mean_sq_scalar = cv::mean(gray_sq);
    variance = mean_sq_scalar[0] - mean * mean;
#endif
}

double CPUSSIMCalculator::calculateCovariance_SSE4_2(const cv::Mat& gray1, const cv::Mat& gray2, double mean1, double mean2) {
#ifdef __SSE4_2__
    const double* data1 = reinterpret_cast<const double*>(gray1.data);
    const double* data2 = reinterpret_cast<const double*>(gray2.data);
    int total = gray1.rows * gray1.cols;

    __m128d sum_vec = _mm_setzero_pd();
    __m128d mean1_vec = _mm_set1_pd(mean1);
    __m128d mean2_vec = _mm_set1_pd(mean2);

    int i = 0;
    for (; i <= total - 2; i += 2) {
        __m128d vals1 = _mm_loadu_pd(&data1[i]);
        __m128d vals2 = _mm_loadu_pd(&data2[i]);

        __m128d diff1 = _mm_sub_pd(vals1, mean1_vec);
        __m128d diff2 = _mm_sub_pd(vals2, mean2_vec);

        sum_vec = _mm_add_pd(sum_vec, _mm_mul_pd(diff1, diff2));
    }

    double sum_array[2];
    _mm_storeu_pd(sum_array, sum_vec);
    double sum = sum_array[0] + sum_array[1];

    // Handle remaining elements
    for (; i < total; ++i) {
        sum += (data1[i] - mean1) * (data2[i] - mean2);
    }

    return sum / total;
#else
    cv::Mat diff1, diff2, product;
    cv::subtract(gray1, cv::Scalar(mean1), diff1);
    cv::subtract(gray2, cv::Scalar(mean2), diff2);
    cv::multiply(diff1, diff2, product);
    cv::Scalar covar_scalar = cv::mean(product);
    return covar_scalar[0];
#endif
}

double CPUSSIMCalculator::calculateCovariance_AVX2(const cv::Mat& gray1, const cv::Mat& gray2, double mean1, double mean2) {
#ifdef __AVX2__
    const double* data1 = reinterpret_cast<const double*>(gray1.data);
    const double* data2 = reinterpret_cast<const double*>(gray2.data);
    int total = gray1.rows * gray1.cols;

    __m256d sum_vec = _mm256_setzero_pd();
    __m256d mean1_vec = _mm256_set1_pd(mean1);
    __m256d mean2_vec = _mm256_set1_pd(mean2);

    int i = 0;
    for (; i <= total - 4; i += 4) {
        __m256d vals1 = _mm256_loadu_pd(&data1[i]);
        __m256d vals2 = _mm256_loadu_pd(&data2[i]);

        __m256d diff1 = _mm256_sub_pd(vals1, mean1_vec);
        __m256d diff2 = _mm256_sub_pd(vals2, mean2_vec);

        sum_vec = _mm256_add_pd(sum_vec, _mm256_mul_pd(diff1, diff2));
    }

    double sum_array[4];
    _mm256_storeu_pd(sum_array, sum_vec);
    double sum = sum_array[0] + sum_array[1] + sum_array[2] + sum_array[3];

    // Handle remaining elements
    for (; i < total; ++i) {
        sum += (data1[i] - mean1) * (data2[i] - mean2);
    }

    return sum / total;
#else
    return calculateCovariance_SSE4_2(gray1, gray2, mean1, mean2);
#endif
}

double CPUSSIMCalculator::calculateCovariance_AVX512(const cv::Mat& gray1, const cv::Mat& gray2, double mean1, double mean2) {
#ifdef __AVX512F__
    const double* data1 = reinterpret_cast<const double*>(gray1.data);
    const double* data2 = reinterpret_cast<const double*>(gray2.data);
    int total = gray1.rows * gray1.cols;

    __m512d sum_vec = _mm512_setzero_pd();
    __m512d mean1_vec = _mm512_set1_pd(mean1);
    __m512d mean2_vec = _mm512_set1_pd(mean2);

    int i = 0;
    for (; i <= total - 8; i += 8) {
        __m512d vals1 = _mm512_loadu_pd(&data1[i]);
        __m512d vals2 = _mm512_loadu_pd(&data2[i]);

        __m512d diff1 = _mm512_sub_pd(vals1, mean1_vec);
        __m512d diff2 = _mm512_sub_pd(vals2, mean2_vec);

        sum_vec = _mm512_add_pd(sum_vec, _mm512_mul_pd(diff1, diff2));
    }

    double sum = _mm512_reduce_add_pd(sum_vec);

    // Handle remaining elements
    for (; i < total; ++i) {
        sum += (data1[i] - mean1) * (data2[i] - mean2);
    }

    return sum / total;
#else
    return calculateCovariance_AVX2(gray1, gray2, mean1, mean2);
#endif
}

double CPUSSIMCalculator::calculateCovariance_NEON(const cv::Mat& gray1, const cv::Mat& gray2, double mean1, double mean2) {
#ifdef __aarch64__
    const double* data1 = reinterpret_cast<const double*>(gray1.data);
    const double* data2 = reinterpret_cast<const double*>(gray2.data);
    int total = gray1.rows * gray1.cols;

    float64x2_t sum_vec = vdupq_n_f64(0.0);
    float64x2_t mean1_vec = vdupq_n_f64(mean1);
    float64x2_t mean2_vec = vdupq_n_f64(mean2);

    int i = 0;
    for (; i <= total - 2; i += 2) {
        float64x2_t vals1 = vld1q_f64(&data1[i]);
        float64x2_t vals2 = vld1q_f64(&data2[i]);

        float64x2_t diff1 = vsubq_f64(vals1, mean1_vec);
        float64x2_t diff2 = vsubq_f64(vals2, mean2_vec);

        sum_vec = vaddq_f64(sum_vec, vmulq_f64(diff1, diff2));
    }

    double sum = vgetq_lane_f64(sum_vec, 0) + vgetq_lane_f64(sum_vec, 1);

    // Handle remaining elements
    for (; i < total; ++i) {
        sum += (data1[i] - mean1) * (data2[i] - mean2);
    }

    return sum / total;
#else
    cv::Mat diff1, diff2, product;
    cv::subtract(gray1, cv::Scalar(mean1), diff1);
    cv::subtract(gray2, cv::Scalar(mean2), diff2);
    cv::multiply(diff1, diff2, product);
    cv::Scalar covar_scalar = cv::mean(product);
    return covar_scalar[0];
#endif
}

// ============================================================================
// PerformanceBenchmark Implementation
// ============================================================================

PerformanceBenchmark::PerformanceBenchmark() {
    // Initialize benchmark system
}

OptimizationMetrics PerformanceBenchmark::benchmarkCalculator(
    SSIMCalculatorBase* calculator,
    const std::vector<cv::Mat>& testImages,
    int iterations) {

    if (!calculator || !calculator->isReady() || testImages.size() < 2) {
        OptimizationMetrics metrics;
        metrics.type = calculator ? calculator->getOptimizationType() : OptimizationType::None;
        metrics.isStable = false;
        metrics.errorMessage = "Invalid calculator or insufficient test images";
        return metrics;
    }

    std::vector<double> timings;
    timings.reserve(iterations);

    // Warm up the calculator
    calculator->warmUp(3);

    // Benchmark iterations
    for (int i = 0; i < iterations; ++i) {
        auto start = std::chrono::high_resolution_clock::now();

        try {
            // Calculate SSIM for consecutive image pairs
            for (size_t j = 1; j < testImages.size(); ++j) {
                double ssim = calculator->calculateSSIM(testImages[j-1], testImages[j]);
                // Validate result
                if (ssim < 0.0 || ssim > 1.0 || std::isnan(ssim) || std::isinf(ssim)) {
                    throw std::runtime_error("Invalid SSIM result: " + std::to_string(ssim));
                }
            }
        } catch (const std::exception& e) {
            OptimizationMetrics metrics;
            metrics.type = calculator->getOptimizationType();
            metrics.isStable = false;
            metrics.errorMessage = "Benchmark failed: " + std::string(e.what());
            return metrics;
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        timings.push_back(duration.count() / 1000.0); // Convert to milliseconds
    }

    return calculateStatistics(timings, calculator->getOptimizationType());
}

std::vector<cv::Mat> PerformanceBenchmark::generateTestImages(int count, int width, int height) {
    std::vector<cv::Mat> images;
    images.reserve(count);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);

    for (int i = 0; i < count; ++i) {
        cv::Mat image(height, width, CV_8UC3);

        // Generate random image with some structure
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                cv::Vec3b& pixel = image.at<cv::Vec3b>(y, x);

                // Add some spatial correlation to make it more realistic
                int base = (x + y + i * 10) % 256;
                pixel[0] = std::max(0, std::min(255, base + dis(gen) % 50 - 25));
                pixel[1] = std::max(0, std::min(255, base + dis(gen) % 50 - 25));
                pixel[2] = std::max(0, std::min(255, base + dis(gen) % 50 - 25));
            }
        }

        images.push_back(image);
    }

    return images;
}

int PerformanceBenchmark::findBestCalculator(
    const std::vector<std::unique_ptr<SSIMCalculatorBase>>& calculators,
    const std::vector<cv::Mat>& testImages,
    int iterations) {

    int bestIndex = -1;
    double bestThroughput = 0.0;

    for (size_t i = 0; i < calculators.size(); ++i) {
        if (!calculators[i] || !calculators[i]->isReady()) {
            continue;
        }

        OptimizationMetrics metrics = benchmarkCalculator(calculators[i].get(), testImages, iterations);
        m_benchmarkResults[metrics.type] = metrics;

        if (metrics.isStable && metrics.throughput > bestThroughput) {
            bestThroughput = metrics.throughput;
            bestIndex = static_cast<int>(i);
        }
    }

    return bestIndex;
}

OptimizationMetrics PerformanceBenchmark::calculateStatistics(const std::vector<double>& timings, OptimizationType type) {
    OptimizationMetrics metrics;
    metrics.type = type;
    metrics.testIterations = static_cast<int>(timings.size());

    if (timings.empty()) {
        metrics.isStable = false;
        metrics.errorMessage = "No timing data available";
        return metrics;
    }

    // Calculate basic statistics
    double sum = std::accumulate(timings.begin(), timings.end(), 0.0);
    metrics.averageExecutionTime = sum / timings.size();

    metrics.minExecutionTime = *std::min_element(timings.begin(), timings.end());
    metrics.maxExecutionTime = *std::max_element(timings.begin(), timings.end());

    // Calculate standard deviation
    double variance = 0.0;
    for (double timing : timings) {
        variance += (timing - metrics.averageExecutionTime) * (timing - metrics.averageExecutionTime);
    }
    variance /= timings.size();
    metrics.standardDeviation = std::sqrt(variance);

    // Calculate throughput (operations per second)
    if (metrics.averageExecutionTime > 0.0) {
        metrics.throughput = 1000.0 / metrics.averageExecutionTime;
    }

    // Determine stability (coefficient of variation < 20%)
    double coefficientOfVariation = metrics.standardDeviation / metrics.averageExecutionTime;
    metrics.isStable = (coefficientOfVariation < 0.2);

    return metrics;
}

// ============================================================================
// OptimizationManager Implementation
// ============================================================================

OptimizationManager& OptimizationManager::getInstance() {
    static OptimizationManager instance;
    return instance;
}

OptimizationManager::OptimizationManager()
    : m_platformDetector(PlatformDetector::getInstance()) {
    m_benchmark = std::make_unique<PerformanceBenchmark>();
}

bool OptimizationManager::initialize(const OptimizationConfig& config) {
    m_config = config;
    m_errorMessage.clear();

    try {
        // Create all available calculators for the platform
        createAvailableCalculators();

        if (m_availableCalculators.empty()) {
            m_errorMessage = "No optimization calculators available for this platform";
            return false;
        }

        // Select the best calculator
        selectBestCalculator();

        if (!m_bestCalculator) {
            m_errorMessage = "Failed to select a working calculator";
            return false;
        }

        m_isInitialized = true;
        logOptimization("OptimizationManager initialized successfully with " +
                       toString(m_activeOptimization));

        return true;
    } catch (const std::exception& e) {
        m_errorMessage = "Initialization failed: " + std::string(e.what());
        return false;
    }
}

void OptimizationManager::createAvailableCalculators() {
    m_availableCalculators.clear();

    // Create CPU calculators based on supported SIMD instruction sets
    createCPUCalculators();

    // Create GPU calculators based on available GPU acceleration
    createGPUCalculators();
}

void OptimizationManager::createCPUCalculators() {
    const auto& supportedSIMD = m_platformDetector.getSupportedSIMD();

    // Create calculators in order of preference (best to worst)
    for (auto simd : supportedSIMD) {
        try {
            auto calculator = std::make_unique<CPUSSIMCalculator>(simd);
            if (calculator->isReady()) {
                m_availableCalculators.push_back(std::move(calculator));
            }
        } catch (const std::exception& e) {
            logOptimization("Failed to create " + PlatformDetector::toString(simd) +
                           " calculator: " + e.what());
        }
    }

    // Always add a generic fallback calculator
    if (std::find(supportedSIMD.begin(), supportedSIMD.end(), SIMDInstructionSet::None) == supportedSIMD.end()) {
        try {
            auto calculator = std::make_unique<CPUSSIMCalculator>(SIMDInstructionSet::None);
            if (calculator->isReady()) {
                m_availableCalculators.push_back(std::move(calculator));
            }
        } catch (const std::exception& e) {
            logOptimization("Failed to create generic calculator: " + std::string(e.what()));
        }
    }
}

void OptimizationManager::createGPUCalculators() {
    // GPU calculators would be implemented here
    // For now, we focus on CPU optimizations

    // TODO: Implement GPU calculators for:
    // - CUDA (NVIDIA GPUs)
    // - OpenCL (Cross-platform)
    // - Metal (Apple GPUs)
    // - DirectX Compute (Windows)
}

void OptimizationManager::selectBestCalculator() {
    if (m_availableCalculators.empty()) {
        return;
    }

    // If benchmarking is enabled, run benchmarks to find the best calculator
    if (m_config.enableBenchmarking) {
        auto testImages = m_benchmark->generateTestImages(5, 640, 480);
        int bestIndex = m_benchmark->findBestCalculator(m_availableCalculators, testImages, m_config.benchmarkIterations);

        if (bestIndex >= 0) {
            m_bestCalculator = m_availableCalculators[bestIndex].get();
            m_activeOptimization = m_bestCalculator->getOptimizationType();
            m_performanceMetrics = m_benchmark->getBenchmarkResults();
            return;
        }
    }

    // Fallback to heuristic selection (first working calculator)
    for (auto& calculator : m_availableCalculators) {
        if (validateCalculator(calculator.get())) {
            m_bestCalculator = calculator.get();
            m_activeOptimization = m_bestCalculator->getOptimizationType();
            return;
        }
    }
}

bool OptimizationManager::validateCalculator(SSIMCalculatorBase* calculator) {
    if (!calculator || !calculator->isReady()) {
        return false;
    }

    try {
        // Create simple test images
        cv::Mat img1 = cv::Mat::ones(100, 100, CV_8UC1) * 128;
        cv::Mat img2 = cv::Mat::ones(100, 100, CV_8UC1) * 128;

        // Test basic functionality
        double ssim = calculator->calculateSSIM(img1, img2);

        // Validate result (should be close to 1.0 for identical images)
        if (ssim < 0.99 || ssim > 1.01 || std::isnan(ssim) || std::isinf(ssim)) {
            return false;
        }

        return true;
    } catch (const std::exception&) {
        return false;
    }
}

SSIMCalculatorBase* OptimizationManager::getBestCalculator() {
    return m_bestCalculator;
}

double OptimizationManager::calculateSSIM(const cv::Mat& img1, const cv::Mat& img2) {
    if (!m_bestCalculator) {
        throw std::runtime_error("OptimizationManager not initialized or no calculator available");
    }

    return m_bestCalculator->calculateSSIM(img1, img2);
}

std::vector<double> OptimizationManager::calculateBatchSSIM(const std::vector<cv::Mat>& images) {
    if (!m_bestCalculator) {
        throw std::runtime_error("OptimizationManager not initialized or no calculator available");
    }

    return m_bestCalculator->calculateBatchSSIM(images);
}

std::vector<OptimizationType> OptimizationManager::getAvailableOptimizations() const {
    std::vector<OptimizationType> types;
    for (const auto& calculator : m_availableCalculators) {
        if (calculator->isReady()) {
            types.push_back(calculator->getOptimizationType());
        }
    }
    return types;
}

bool OptimizationManager::forceOptimization(OptimizationType type) {
    for (auto& calculator : m_availableCalculators) {
        if (calculator->getOptimizationType() == type && calculator->isReady()) {
            m_bestCalculator = calculator.get();
            m_activeOptimization = type;
            m_forcedOptimization = true;
            logOptimization("Forced optimization to " + toString(type));
            return true;
        }
    }
    return false;
}

void OptimizationManager::resetToAutomatic() {
    m_forcedOptimization = false;
    selectBestCalculator();
    logOptimization("Reset to automatic optimization selection: " + toString(m_activeOptimization));
}

bool OptimizationManager::runBenchmarks(int testImageCount, int iterations) {
    if (m_availableCalculators.empty()) {
        return false;
    }

    try {
        auto testImages = m_benchmark->generateTestImages(testImageCount, 1920, 1080);
        m_benchmark->findBestCalculator(m_availableCalculators, testImages, iterations);
        m_performanceMetrics = m_benchmark->getBenchmarkResults();

        // Update best calculator based on benchmark results
        selectBestCalculator();

        return true;
    } catch (const std::exception& e) {
        m_errorMessage = "Benchmark failed: " + std::string(e.what());
        return false;
    }
}

bool OptimizationManager::updateConfig(const OptimizationConfig& config) {
    m_config = config;

    // Re-initialize if configuration significantly changed
    if (config.enableBenchmarking != m_config.enableBenchmarking) {
        return initialize(config);
    }

    return true;
}

std::string OptimizationManager::generateOptimizationReport() const {
    std::ostringstream oss;

    oss << "=== Optimization Manager Report ===\n\n";

    // Platform information
    oss << m_platformDetector.getPlatformSummary() << "\n";

    // Active optimization
    oss << "Active Optimization: " << toString(m_activeOptimization) << "\n";
    oss << "Forced Optimization: " << (m_forcedOptimization ? "Yes" : "No") << "\n\n";

    // Available optimizations
    oss << "Available Optimizations:\n";
    for (const auto& calculator : m_availableCalculators) {
        oss << "  - " << toString(calculator->getOptimizationType());
        if (calculator->isReady()) {
            oss << " (Ready)";
        } else {
            oss << " (Error: " << calculator->getErrorMessage() << ")";
        }
        oss << "\n";
    }
    oss << "\n";

    // Performance metrics
    if (!m_performanceMetrics.empty()) {
        oss << "Performance Metrics:\n";
        for (const auto& [type, metrics] : m_performanceMetrics) {
            oss << "  " << toString(type) << ":\n";
            oss << "    Avg Time: " << std::fixed << std::setprecision(2)
                << metrics.averageExecutionTime << " ms\n";
            oss << "    Throughput: " << std::fixed << std::setprecision(1)
                << metrics.throughput << " ops/sec\n";
            oss << "    Stable: " << (metrics.isStable ? "Yes" : "No") << "\n";
            if (!metrics.errorMessage.empty()) {
                oss << "    Error: " << metrics.errorMessage << "\n";
            }
            oss << "\n";
        }
    }

    return oss.str();
}

void OptimizationManager::logOptimization(const std::string& message) {
    if (m_config.enablePerformanceLogging) {
        std::cout << "[OptimizationManager] " << message << std::endl;
    }
}

std::string OptimizationManager::toString(OptimizationType type) {
    switch (type) {
        case OptimizationType::None: return "None";
        case OptimizationType::SIMD_SSE4_2: return "SIMD SSE4.2";
        case OptimizationType::SIMD_AVX2: return "SIMD AVX2";
        case OptimizationType::SIMD_AVX512: return "SIMD AVX512";
        case OptimizationType::SIMD_NEON: return "SIMD NEON";
        case OptimizationType::GPU_CUDA: return "GPU CUDA";
        case OptimizationType::GPU_OpenCL: return "GPU OpenCL";
        case OptimizationType::GPU_Metal: return "GPU Metal";
        case OptimizationType::GPU_DirectX: return "GPU DirectX";
        case OptimizationType::GPU_Vulkan: return "GPU Vulkan";
        case OptimizationType::Hybrid_CPU_GPU: return "Hybrid CPU+GPU";
        default: return "Unknown";
    }
}