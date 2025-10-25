#ifndef GPUACCELERATION_H
#define GPUACCELERATION_H

#include "optimizationmanager.h"
#include <opencv2/opencv.hpp>
#include <memory>
#include <vector>
#include <string>

// Forward declarations for GPU-specific types
#ifdef CUDA_AVAILABLE
    struct CUstream_st;
    typedef CUstream_st* cudaStream_t;
    struct CUctx_st;
    typedef CUctx_st* CUcontext;
#endif

#ifdef OPENCL_AVAILABLE
    #ifdef __APPLE__
        #include <OpenCL/opencl.h>
    #else
        #include <CL/cl.h>
    #endif
#endif

#ifdef __APPLE__
    #ifdef __OBJC__
        @class MTLDevice;
        @class MTLCommandQueue;
        @class MTLComputePipelineState;
        @class MTLBuffer;
    #else
        typedef struct objc_object MTLDevice;
        typedef struct objc_object MTLCommandQueue;
        typedef struct objc_object MTLComputePipelineState;
        typedef struct objc_object MTLBuffer;
    #endif
#endif

#ifdef _WIN32
    struct ID3D11Device;
    struct ID3D11DeviceContext;
    struct ID3D11ComputeShader;
    struct ID3D11Buffer;
    struct ID3D11UnorderedAccessView;
#endif

/**
 * Base class for GPU-accelerated SSIM calculators
 */
class GPUSSIMCalculator : public SSIMCalculatorBase {
public:
    GPUSSIMCalculator() = default;
    ~GPUSSIMCalculator() override = default;

    /**
     * Get GPU memory usage in bytes
     * @return Memory usage in bytes
     */
    size_t getMemoryUsage() const override = 0;

    /**
     * Get GPU device information
     * @return String containing device information
     */
    virtual std::string getDeviceInfo() const = 0;

    /**
     * Check if GPU has sufficient memory for operation
     * @param requiredMemory Required memory in bytes
     * @return true if sufficient memory is available
     */
    virtual bool hasSufficientMemory(size_t requiredMemory) const = 0;

protected:
    /**
     * Calculate required GPU memory for given image dimensions
     * @param width Image width
     * @param height Image height
     * @param channels Number of channels
     * @return Required memory in bytes
     */
    size_t calculateRequiredMemory(int width, int height, int channels) const;
};

#ifdef CUDA_AVAILABLE
/**
 * CUDA-accelerated SSIM calculator for NVIDIA GPUs
 */
class CUDASSIMCalculator : public GPUSSIMCalculator {
public:
    CUDASSIMCalculator();
    ~CUDASSIMCalculator() override;

    // SSIMCalculatorBase interface
    double calculateSSIM(const cv::Mat& img1, const cv::Mat& img2) override;
    std::vector<double> calculateBatchSSIM(const std::vector<cv::Mat>& images) override;
    OptimizationType getOptimizationType() const override { return OptimizationType::GPU_CUDA; }
    bool isReady() const override { return m_isInitialized; }
    std::string getErrorMessage() const override { return m_errorMessage; }
    void warmUp(int warmupIterations = 3) override;

    // GPUSSIMCalculator interface
    size_t getMemoryUsage() const override { return m_memoryUsage; }
    std::string getDeviceInfo() const override;
    bool hasSufficientMemory(size_t requiredMemory) const override;

private:
    bool initializeCUDA();
    void cleanup();
    bool allocateBuffers(int width, int height);
    void deallocateBuffers();

    double calculateSSIMCUDA(const cv::Mat& img1, const cv::Mat& img2);
    bool uploadImageToGPU(const cv::Mat& image, float* gpuBuffer);
    bool downloadResultFromGPU(float* gpuBuffer, double& result);

    // CUDA resources
    int m_deviceId = -1;
    CUcontext m_context = nullptr;
    cudaStream_t m_stream = nullptr;

    // GPU memory buffers
    float* m_gpuBuffer1 = nullptr;
    float* m_gpuBuffer2 = nullptr;
    float* m_gpuWorkBuffer = nullptr;
    float* m_gpuResultBuffer = nullptr;

    // Buffer management
    int m_bufferWidth = 0;
    int m_bufferHeight = 0;
    size_t m_memoryUsage = 0;
    size_t m_deviceMemoryTotal = 0;
    size_t m_deviceMemoryFree = 0;

    bool m_isInitialized = false;
    std::string m_errorMessage;
    std::string m_deviceName;
};
#endif // CUDA_AVAILABLE

#ifdef OPENCL_AVAILABLE
/**
 * OpenCL-accelerated SSIM calculator for cross-platform GPU acceleration
 */
class OpenCLSSIMCalculator : public GPUSSIMCalculator {
public:
    OpenCLSSIMCalculator();
    ~OpenCLSSIMCalculator() override;

    // SSIMCalculatorBase interface
    double calculateSSIM(const cv::Mat& img1, const cv::Mat& img2) override;
    std::vector<double> calculateBatchSSIM(const std::vector<cv::Mat>& images) override;
    OptimizationType getOptimizationType() const override { return OptimizationType::GPU_OpenCL; }
    bool isReady() const override { return m_isInitialized; }
    std::string getErrorMessage() const override { return m_errorMessage; }
    void warmUp(int warmupIterations = 3) override;

    // GPUSSIMCalculator interface
    size_t getMemoryUsage() const override { return m_memoryUsage; }
    std::string getDeviceInfo() const override;
    bool hasSufficientMemory(size_t requiredMemory) const override;

private:
    bool initializeOpenCL();
    void cleanup();
    bool createKernels();
    bool allocateBuffers(int width, int height);
    void deallocateBuffers();

    double calculateSSIMOpenCL(const cv::Mat& img1, const cv::Mat& img2);
    bool uploadImageToGPU(const cv::Mat& image, cl_mem buffer);
    bool downloadResultFromGPU(cl_mem buffer, double& result);

    // OpenCL resources
    cl_platform_id m_platform = nullptr;
    cl_device_id m_device = nullptr;
    cl_context m_context = nullptr;
    cl_command_queue m_commandQueue = nullptr;
    cl_program m_program = nullptr;

    // OpenCL kernels
    cl_kernel m_ssimKernel = nullptr;
    cl_kernel m_meanKernel = nullptr;
    cl_kernel m_varianceKernel = nullptr;
    cl_kernel m_covarianceKernel = nullptr;

    // GPU memory buffers
    cl_mem m_bufferImg1 = nullptr;
    cl_mem m_bufferImg2 = nullptr;
    cl_mem m_bufferWork = nullptr;
    cl_mem m_bufferResult = nullptr;

    // Buffer management
    int m_bufferWidth = 0;
    int m_bufferHeight = 0;
    size_t m_memoryUsage = 0;
    size_t m_deviceMemoryTotal = 0;

    bool m_isInitialized = false;
    std::string m_errorMessage;
    std::string m_deviceName;

    // OpenCL kernel source code
    static const char* getSSIMKernelSource();
};
#endif // OPENCL_AVAILABLE

#ifdef __APPLE__
/**
 * Metal-accelerated SSIM calculator for Apple GPUs
 */
class MetalSSIMCalculator : public GPUSSIMCalculator {
public:
    MetalSSIMCalculator();
    ~MetalSSIMCalculator() override;

    // SSIMCalculatorBase interface
    double calculateSSIM(const cv::Mat& img1, const cv::Mat& img2) override;
    std::vector<double> calculateBatchSSIM(const std::vector<cv::Mat>& images) override;
    OptimizationType getOptimizationType() const override { return OptimizationType::GPU_Metal; }
    bool isReady() const override { return m_isInitialized; }
    std::string getErrorMessage() const override { return m_errorMessage; }
    void warmUp(int warmupIterations = 3) override;

    // GPUSSIMCalculator interface
    size_t getMemoryUsage() const override { return m_memoryUsage; }
    std::string getDeviceInfo() const override;
    bool hasSufficientMemory(size_t requiredMemory) const override;

private:
    bool initializeMetal();
    void cleanup();
    bool createComputePipeline();
    bool allocateBuffers(int width, int height);
    void deallocateBuffers();

    double calculateSSIMMetal(const cv::Mat& img1, const cv::Mat& img2);
    bool uploadImageToGPU(const cv::Mat& image, MTLBuffer* buffer);
    bool downloadResultFromGPU(MTLBuffer* buffer, double& result);

    // Metal resources (using void* to avoid Objective-C in header)
    MTLDevice* m_device = nullptr;
    MTLCommandQueue* m_commandQueue = nullptr;
    MTLComputePipelineState* m_ssimPipeline = nullptr;

    // GPU memory buffers
    MTLBuffer* m_bufferImg1 = nullptr;
    MTLBuffer* m_bufferImg2 = nullptr;
    MTLBuffer* m_bufferWork = nullptr;
    MTLBuffer* m_bufferResult = nullptr;

    // Buffer management
    int m_bufferWidth = 0;
    int m_bufferHeight = 0;
    size_t m_memoryUsage = 0;
    size_t m_deviceMemoryTotal = 0;

    bool m_isInitialized = false;
    std::string m_errorMessage;
    std::string m_deviceName;

    // Metal shader source code
    static const char* getSSIMShaderSource();
};
#endif // __APPLE__

#ifdef _WIN32
/**
 * DirectX Compute Shader-accelerated SSIM calculator for Windows
 */
class DirectXSSIMCalculator : public GPUSSIMCalculator {
public:
    DirectXSSIMCalculator();
    ~DirectXSSIMCalculator() override;

    // SSIMCalculatorBase interface
    double calculateSSIM(const cv::Mat& img1, const cv::Mat& img2) override;
    std::vector<double> calculateBatchSSIM(const std::vector<cv::Mat>& images) override;
    OptimizationType getOptimizationType() const override { return OptimizationType::GPU_DirectX; }
    bool isReady() const override { return m_isInitialized; }
    std::string getErrorMessage() const override { return m_errorMessage; }
    void warmUp(int warmupIterations = 3) override;

    // GPUSSIMCalculator interface
    size_t getMemoryUsage() const override { return m_memoryUsage; }
    std::string getDeviceInfo() const override;
    bool hasSufficientMemory(size_t requiredMemory) const override;

private:
    bool initializeDirectX();
    void cleanup();
    bool createComputeShader();
    bool allocateBuffers(int width, int height);
    void deallocateBuffers();

    double calculateSSIMDirectX(const cv::Mat& img1, const cv::Mat& img2);
    bool uploadImageToGPU(const cv::Mat& image, ID3D11Buffer* buffer);
    bool downloadResultFromGPU(ID3D11Buffer* buffer, double& result);

    // DirectX resources
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_deviceContext = nullptr;
    ID3D11ComputeShader* m_ssimShader = nullptr;

    // GPU memory buffers
    ID3D11Buffer* m_bufferImg1 = nullptr;
    ID3D11Buffer* m_bufferImg2 = nullptr;
    ID3D11Buffer* m_bufferWork = nullptr;
    ID3D11Buffer* m_bufferResult = nullptr;
    ID3D11Buffer* m_stagingBuffer = nullptr;

    // Unordered Access Views
    ID3D11UnorderedAccessView* m_uavImg1 = nullptr;
    ID3D11UnorderedAccessView* m_uavImg2 = nullptr;
    ID3D11UnorderedAccessView* m_uavWork = nullptr;
    ID3D11UnorderedAccessView* m_uavResult = nullptr;

    // Buffer management
    int m_bufferWidth = 0;
    int m_bufferHeight = 0;
    size_t m_memoryUsage = 0;
    size_t m_deviceMemoryTotal = 0;

    bool m_isInitialized = false;
    std::string m_errorMessage;
    std::string m_deviceName;

    // DirectX shader source code
    static const char* getSSIMShaderSource();
};
#endif // _WIN32

/**
 * Factory class for creating GPU-accelerated SSIM calculators
 */
class GPUCalculatorFactory {
public:
    /**
     * Create the best available GPU calculator for the current platform
     * @return Unique pointer to GPU calculator, or nullptr if none available
     */
    static std::unique_ptr<GPUSSIMCalculator> createBestGPUCalculator();

    /**
     * Create a specific GPU calculator type
     * @param type GPU acceleration type
     * @return Unique pointer to GPU calculator, or nullptr if not available
     */
    static std::unique_ptr<GPUSSIMCalculator> createGPUCalculator(GPUAccelerationType type);

    /**
     * Get available GPU acceleration types for the current platform
     * @return Vector of available GPU acceleration types
     */
    static std::vector<GPUAccelerationType> getAvailableGPUTypes();

    /**
     * Check if a specific GPU acceleration type is available
     * @param type GPU acceleration type to check
     * @return true if available
     */
    static bool isGPUTypeAvailable(GPUAccelerationType type);

private:
    // Platform-specific availability checks
    static bool isCUDAAvailable();
    static bool isOpenCLAvailable();
    static bool isMetalAvailable();
    static bool isDirectXAvailable();
    static bool isVulkanAvailable();
};

#endif // GPUACCELERATION_H