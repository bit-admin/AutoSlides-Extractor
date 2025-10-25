#ifndef OPTIMIZATIONMANAGER_H
#define OPTIMIZATIONMANAGER_H

#include "platformdetector.h"
#include <memory>
#include <functional>
#include <chrono>
#include <vector>
#include <unordered_map>
#include <string>
#include <opencv2/opencv.hpp>

// Forward declarations
class SSIMCalculatorBase;
class GPUSSIMCalculator;
class PerformanceBenchmark;

/**
 * Enumeration of available optimization types
 */
enum class OptimizationType {
    None,
    SIMD_SSE4_2,
    SIMD_AVX2,
    SIMD_AVX512,
    SIMD_NEON,
    GPU_CUDA,
    GPU_OpenCL,
    GPU_Metal,
    GPU_DirectX,
    GPU_Vulkan,
    Hybrid_CPU_GPU
};

/**
 * Structure containing optimization performance metrics
 */
struct OptimizationMetrics {
    OptimizationType type = OptimizationType::None;
    double averageExecutionTime = 0.0;  // in milliseconds
    double throughput = 0.0;             // operations per second
    double memoryUsage = 0.0;            // in MB
    double powerEfficiency = 0.0;        // operations per watt (if available)
    bool isStable = true;                // whether the optimization is stable
    std::string errorMessage;            // error message if optimization failed

    // Benchmark details
    int testIterations = 0;
    double minExecutionTime = 0.0;
    double maxExecutionTime = 0.0;
    double standardDeviation = 0.0;
};

/**
 * Structure containing optimization configuration
 */
struct OptimizationConfig {
    OptimizationType primaryOptimization = OptimizationType::None;
    OptimizationType fallbackOptimization = OptimizationType::None;
    bool enableAutoFallback = true;
    bool enablePerformanceLogging = true;
    bool enableBenchmarking = false;
    int benchmarkIterations = 10;
    double performanceThreshold = 1.0;   // minimum performance multiplier to use optimization

    // Memory constraints
    size_t maxGPUMemoryUsage = 0;        // 0 = no limit
    size_t maxSystemMemoryUsage = 0;     // 0 = no limit

    // Threading configuration
    int maxThreads = 0;                  // 0 = auto-detect
    bool enableHyperThreading = true;
};

/**
 * Base class for SSIM calculators with different optimizations
 */
class SSIMCalculatorBase {
public:
    virtual ~SSIMCalculatorBase() = default;

    /**
     * Calculate SSIM between two images
     * @param img1 First image
     * @param img2 Second image
     * @return SSIM score between 0 and 1
     */
    virtual double calculateSSIM(const cv::Mat& img1, const cv::Mat& img2) = 0;

    /**
     * Calculate SSIM for a batch of image pairs
     * @param images Vector of images to process consecutively
     * @return Vector of SSIM scores
     */
    virtual std::vector<double> calculateBatchSSIM(const std::vector<cv::Mat>& images) = 0;

    /**
     * Get the optimization type used by this calculator
     * @return Optimization type
     */
    virtual OptimizationType getOptimizationType() const = 0;

    /**
     * Check if the calculator is ready for use
     * @return true if ready
     */
    virtual bool isReady() const = 0;

    /**
     * Get initialization error message if any
     * @return Error message or empty string if no error
     */
    virtual std::string getErrorMessage() const = 0;

    /**
     * Warm up the calculator (useful for GPU calculators)
     * @param warmupIterations Number of warmup iterations
     */
    virtual void warmUp(int warmupIterations = 3) {}

    /**
     * Get memory usage statistics
     * @return Memory usage in bytes
     */
    virtual size_t getMemoryUsage() const { return 0; }
};

/**
 * CPU-based SSIM calculator with SIMD optimizations
 */
class CPUSSIMCalculator : public SSIMCalculatorBase {
public:
    explicit CPUSSIMCalculator(SIMDInstructionSet instructionSet);
    ~CPUSSIMCalculator() override = default;

    double calculateSSIM(const cv::Mat& img1, const cv::Mat& img2) override;
    std::vector<double> calculateBatchSSIM(const std::vector<cv::Mat>& images) override;
    OptimizationType getOptimizationType() const override;
    bool isReady() const override { return m_isReady; }
    std::string getErrorMessage() const override { return m_errorMessage; }

private:
    SIMDInstructionSet m_instructionSet;
    bool m_isReady = false;
    std::string m_errorMessage;

    // SIMD-specific calculation methods
    double calculateSSIM_Generic(const cv::Mat& img1, const cv::Mat& img2);
    double calculateSSIM_SSE4_2(const cv::Mat& img1, const cv::Mat& img2);
    double calculateSSIM_AVX2(const cv::Mat& img1, const cv::Mat& img2);
    double calculateSSIM_AVX512(const cv::Mat& img1, const cv::Mat& img2);
    double calculateSSIM_NEON(const cv::Mat& img1, const cv::Mat& img2);

    // Helper methods for SIMD calculations
    void calculateMeanVariance_SSE4_2(const cv::Mat& gray, double& mean, double& variance);
    void calculateMeanVariance_AVX2(const cv::Mat& gray, double& mean, double& variance);
    void calculateMeanVariance_AVX512(const cv::Mat& gray, double& mean, double& variance);
    void calculateMeanVariance_NEON(const cv::Mat& gray, double& mean, double& variance);

    double calculateCovariance_SSE4_2(const cv::Mat& gray1, const cv::Mat& gray2, double mean1, double mean2);
    double calculateCovariance_AVX2(const cv::Mat& gray1, const cv::Mat& gray2, double mean1, double mean2);
    double calculateCovariance_AVX512(const cv::Mat& gray1, const cv::Mat& gray2, double mean1, double mean2);
    double calculateCovariance_NEON(const cv::Mat& gray1, const cv::Mat& gray2, double mean1, double mean2);
};

/**
 * Performance benchmarking system for optimization selection
 */
class PerformanceBenchmark {
public:
    PerformanceBenchmark();
    ~PerformanceBenchmark() = default;

    /**
     * Benchmark a specific SSIM calculator
     * @param calculator Calculator to benchmark
     * @param testImages Vector of test images for benchmarking
     * @param iterations Number of benchmark iterations
     * @return Performance metrics
     */
    OptimizationMetrics benchmarkCalculator(
        SSIMCalculatorBase* calculator,
        const std::vector<cv::Mat>& testImages,
        int iterations = 10
    );

    /**
     * Generate test images for benchmarking
     * @param count Number of test images to generate
     * @param width Image width
     * @param height Image height
     * @return Vector of test images
     */
    std::vector<cv::Mat> generateTestImages(int count = 10, int width = 1920, int height = 1080);

    /**
     * Compare multiple calculators and return the best one
     * @param calculators Vector of calculators to compare
     * @param testImages Test images for benchmarking
     * @param iterations Benchmark iterations per calculator
     * @return Index of the best calculator, or -1 if none are suitable
     */
    int findBestCalculator(
        const std::vector<std::unique_ptr<SSIMCalculatorBase>>& calculators,
        const std::vector<cv::Mat>& testImages,
        int iterations = 10
    );

    /**
     * Get detailed benchmark results
     * @return Map of optimization type to metrics
     */
    const std::unordered_map<OptimizationType, OptimizationMetrics>& getBenchmarkResults() const {
        return m_benchmarkResults;
    }

    /**
     * Clear benchmark results
     */
    void clearResults() { m_benchmarkResults.clear(); }

private:
    std::unordered_map<OptimizationType, OptimizationMetrics> m_benchmarkResults;

    /**
     * Calculate statistics from timing measurements
     * @param timings Vector of timing measurements in milliseconds
     * @return Metrics with calculated statistics
     */
    OptimizationMetrics calculateStatistics(const std::vector<double>& timings, OptimizationType type);
};

/**
 * Main optimization management system
 *
 * This class automatically detects the best available optimizations for the current platform
 * and provides a unified interface for SSIM calculations with automatic fallback support.
 */
class OptimizationManager {
public:
    /**
     * Get the singleton instance of OptimizationManager
     * @return Reference to the singleton instance
     */
    static OptimizationManager& getInstance();

    /**
     * Initialize the optimization manager with configuration
     * @param config Optimization configuration
     * @return true if initialization was successful
     */
    bool initialize(const OptimizationConfig& config = OptimizationConfig{});

    /**
     * Get the best available SSIM calculator for the current platform
     * @return Pointer to the best calculator, or nullptr if none available
     */
    SSIMCalculatorBase* getBestCalculator();

    /**
     * Calculate SSIM using the best available optimization
     * @param img1 First image
     * @param img2 Second image
     * @return SSIM score between 0 and 1
     */
    double calculateSSIM(const cv::Mat& img1, const cv::Mat& img2);

    /**
     * Calculate SSIM for a batch of images using the best available optimization
     * @param images Vector of images to process consecutively
     * @return Vector of SSIM scores
     */
    std::vector<double> calculateBatchSSIM(const std::vector<cv::Mat>& images);

    /**
     * Get the currently active optimization type
     * @return Active optimization type
     */
    OptimizationType getActiveOptimization() const { return m_activeOptimization; }

    /**
     * Get available optimization types for the current platform
     * @return Vector of available optimization types
     */
    std::vector<OptimizationType> getAvailableOptimizations() const;

    /**
     * Force a specific optimization type (bypasses automatic selection)
     * @param type Optimization type to use
     * @return true if the optimization was successfully activated
     */
    bool forceOptimization(OptimizationType type);

    /**
     * Reset to automatic optimization selection
     */
    void resetToAutomatic();

    /**
     * Get performance metrics for all tested optimizations
     * @return Map of optimization type to metrics
     */
    const std::unordered_map<OptimizationType, OptimizationMetrics>& getPerformanceMetrics() const {
        return m_performanceMetrics;
    }

    /**
     * Run comprehensive benchmarks on all available optimizations
     * @param testImageCount Number of test images to generate
     * @param iterations Benchmark iterations per optimization
     * @return true if benchmarking completed successfully
     */
    bool runBenchmarks(int testImageCount = 10, int iterations = 10);

    /**
     * Get optimization configuration
     * @return Current optimization configuration
     */
    const OptimizationConfig& getConfig() const { return m_config; }

    /**
     * Update optimization configuration
     * @param config New configuration
     * @return true if configuration was applied successfully
     */
    bool updateConfig(const OptimizationConfig& config);

    /**
     * Get platform information
     * @return Reference to platform detector
     */
    const PlatformDetector& getPlatformDetector() const { return m_platformDetector; }

    /**
     * Generate optimization report
     * @return String containing detailed optimization information
     */
    std::string generateOptimizationReport() const;

    /**
     * Check if the optimization manager is ready for use
     * @return true if ready
     */
    bool isReady() const { return m_isInitialized && m_bestCalculator != nullptr; }

    /**
     * Get initialization error message if any
     * @return Error message or empty string if no error
     */
    std::string getErrorMessage() const { return m_errorMessage; }

    /**
     * Convert optimization type to string
     * @param type Optimization type
     * @return String representation
     */
    static std::string toString(OptimizationType type);

private:
    /**
     * Private constructor for singleton pattern
     */
    OptimizationManager();

    /**
     * Destructor
     */
    ~OptimizationManager() = default;

    /**
     * Deleted copy constructor and assignment operator
     */
    OptimizationManager(const OptimizationManager&) = delete;
    OptimizationManager& operator=(const OptimizationManager&) = delete;

    /**
     * Create all available calculators for the current platform
     */
    void createAvailableCalculators();

    /**
     * Create CPU-based calculators
     */
    void createCPUCalculators();

    /**
     * Create GPU-based calculators
     */
    void createGPUCalculators();

    /**
     * Select the best calculator based on benchmarks or heuristics
     */
    void selectBestCalculator();

    /**
     * Validate calculator functionality
     * @param calculator Calculator to validate
     * @return true if calculator is functional
     */
    bool validateCalculator(SSIMCalculatorBase* calculator);

    /**
     * Log optimization selection and performance
     * @param message Log message
     */
    void logOptimization(const std::string& message);

    // Member variables
    PlatformDetector& m_platformDetector;
    OptimizationConfig m_config;
    std::vector<std::unique_ptr<SSIMCalculatorBase>> m_availableCalculators;
    SSIMCalculatorBase* m_bestCalculator = nullptr;
    OptimizationType m_activeOptimization = OptimizationType::None;
    std::unordered_map<OptimizationType, OptimizationMetrics> m_performanceMetrics;
    std::unique_ptr<PerformanceBenchmark> m_benchmark;

    bool m_isInitialized = false;
    bool m_forcedOptimization = false;
    std::string m_errorMessage;

    // Constants
    static constexpr double SSIM_C1 = 6.5025;   // (0.01 * 255)^2
    static constexpr double SSIM_C2 = 58.5225;  // (0.03 * 255)^2
};

#endif // OPTIMIZATIONMANAGER_H