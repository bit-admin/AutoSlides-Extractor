#include "gpuacceleration.h"
#include "platformdetector.h"
#include <iostream>
#include <algorithm>
#include <cmath>

// Platform-specific includes
#ifdef CUDA_AVAILABLE
    #include <cuda_runtime.h>
    #include <cuda.h>
    #include <nvml.h>
#endif

#ifdef OPENCL_AVAILABLE
    #ifdef __APPLE__
        #include <OpenCL/opencl.h>
    #else
        #include <CL/cl.h>
    #endif
#endif

#ifdef __APPLE__
    // #include <Metal/Metal.h>  // Causes C++ compilation issues
    // #include <Foundation/Foundation.h>  // Causes C++ compilation issues
#endif

#ifdef _WIN32
    #include <d3d11.h>
    #include <d3dcompiler.h>
    #include <dxgi.h>
    #pragma comment(lib, "d3d11.lib")
    #pragma comment(lib, "d3dcompiler.lib")
    #pragma comment(lib, "dxgi.lib")
#endif

// ============================================================================
// GPUSSIMCalculator Base Implementation
// ============================================================================

size_t GPUSSIMCalculator::calculateRequiredMemory(int width, int height, int channels) const {
    // Calculate memory for input images, work buffers, and results
    size_t imageSize = width * height * channels * sizeof(float);
    size_t workBufferSize = width * height * sizeof(float);
    size_t resultSize = sizeof(double);

    // Total: 2 input images + 2 work buffers + result buffer
    return 2 * imageSize + 2 * workBufferSize + resultSize;
}

// ============================================================================
// CUDA Implementation
// ============================================================================

#ifdef CUDA_AVAILABLE

// CUDA kernel declarations
extern "C" {
    void launchSSIMKernel(float* img1, float* img2, float* result, int width, int height, cudaStream_t stream);
    void launchMeanKernel(float* image, float* result, int width, int height, cudaStream_t stream);
    void launchVarianceKernel(float* image, float mean, float* result, int width, int height, cudaStream_t stream);
    void launchCovarianceKernel(float* img1, float* img2, float mean1, float mean2, float* result, int width, int height, cudaStream_t stream);
}

CUDASSIMCalculator::CUDASSIMCalculator() {
    if (!initializeCUDA()) {
        m_errorMessage = "Failed to initialize CUDA";
        m_isInitialized = false;
    }
}

CUDASSIMCalculator::~CUDASSIMCalculator() {
    cleanup();
}

bool CUDASSIMCalculator::initializeCUDA() {
    try {
        // Initialize CUDA
        cudaError_t error = cudaGetDeviceCount(&m_deviceId);
        if (error != cudaSuccess || m_deviceId == 0) {
            m_errorMessage = "No CUDA devices found";
            return false;
        }

        // Use the first available device
        m_deviceId = 0;
        error = cudaSetDevice(m_deviceId);
        if (error != cudaSuccess) {
            m_errorMessage = "Failed to set CUDA device";
            return false;
        }

        // Get device properties
        cudaDeviceProp deviceProp;
        error = cudaGetDeviceProperties(&deviceProp, m_deviceId);
        if (error != cudaSuccess) {
            m_errorMessage = "Failed to get device properties";
            return false;
        }

        m_deviceName = std::string(deviceProp.name);
        m_deviceMemoryTotal = deviceProp.totalGlobalMem;

        // Create CUDA stream
        error = cudaStreamCreate(&m_stream);
        if (error != cudaSuccess) {
            m_errorMessage = "Failed to create CUDA stream";
            return false;
        }

        m_isInitialized = true;
        return true;

    } catch (const std::exception& e) {
        m_errorMessage = "CUDA initialization exception: " + std::string(e.what());
        return false;
    }
}

void CUDASSIMCalculator::cleanup() {
    deallocateBuffers();

    if (m_stream) {
        cudaStreamDestroy(m_stream);
        m_stream = nullptr;
    }

    if (m_isInitialized) {
        cudaDeviceReset();
        m_isInitialized = false;
    }
}

bool CUDASSIMCalculator::allocateBuffers(int width, int height) {
    if (m_bufferWidth == width && m_bufferHeight == height && m_gpuBuffer1) {
        return true; // Buffers already allocated for this size
    }

    deallocateBuffers();

    size_t imageSize = width * height * sizeof(float);
    size_t workSize = width * height * sizeof(float);

    cudaError_t error;

    // Allocate GPU buffers
    error = cudaMalloc(&m_gpuBuffer1, imageSize);
    if (error != cudaSuccess) {
        m_errorMessage = "Failed to allocate GPU buffer 1";
        return false;
    }

    error = cudaMalloc(&m_gpuBuffer2, imageSize);
    if (error != cudaSuccess) {
        m_errorMessage = "Failed to allocate GPU buffer 2";
        return false;
    }

    error = cudaMalloc(&m_gpuWorkBuffer, workSize);
    if (error != cudaSuccess) {
        m_errorMessage = "Failed to allocate GPU work buffer";
        return false;
    }

    error = cudaMalloc(&m_gpuResultBuffer, sizeof(float));
    if (error != cudaSuccess) {
        m_errorMessage = "Failed to allocate GPU result buffer";
        return false;
    }

    m_bufferWidth = width;
    m_bufferHeight = height;
    m_memoryUsage = 2 * imageSize + workSize + sizeof(float);

    return true;
}

void CUDASSIMCalculator::deallocateBuffers() {
    if (m_gpuBuffer1) {
        cudaFree(m_gpuBuffer1);
        m_gpuBuffer1 = nullptr;
    }
    if (m_gpuBuffer2) {
        cudaFree(m_gpuBuffer2);
        m_gpuBuffer2 = nullptr;
    }
    if (m_gpuWorkBuffer) {
        cudaFree(m_gpuWorkBuffer);
        m_gpuWorkBuffer = nullptr;
    }
    if (m_gpuResultBuffer) {
        cudaFree(m_gpuResultBuffer);
        m_gpuResultBuffer = nullptr;
    }

    m_bufferWidth = 0;
    m_bufferHeight = 0;
    m_memoryUsage = 0;
}

double CUDASSIMCalculator::calculateSSIM(const cv::Mat& img1, const cv::Mat& img2) {
    if (!m_isInitialized) {
        return 0.0;
    }

    return calculateSSIMCUDA(img1, img2);
}

std::vector<double> CUDASSIMCalculator::calculateBatchSSIM(const std::vector<cv::Mat>& images) {
    std::vector<double> results;
    results.reserve(images.size() - 1);

    for (size_t i = 1; i < images.size(); ++i) {
        results.push_back(calculateSSIM(images[i-1], images[i]));
    }

    return results;
}

void CUDASSIMCalculator::warmUp(int warmupIterations) {
    if (!m_isInitialized) return;

    // Create small test images for warmup
    cv::Mat testImg1 = cv::Mat::ones(256, 256, CV_8UC1) * 128;
    cv::Mat testImg2 = cv::Mat::ones(256, 256, CV_8UC1) * 130;

    for (int i = 0; i < warmupIterations; ++i) {
        calculateSSIM(testImg1, testImg2);
    }

    // Synchronize to ensure warmup is complete
    cudaStreamSynchronize(m_stream);
}

std::string CUDASSIMCalculator::getDeviceInfo() const {
    return "CUDA Device: " + m_deviceName +
           " (Memory: " + std::to_string(m_deviceMemoryTotal / (1024*1024)) + " MB)";
}

bool CUDASSIMCalculator::hasSufficientMemory(size_t requiredMemory) const {
    size_t freeMemory, totalMemory;
    cudaError_t error = cudaMemGetInfo(&freeMemory, &totalMemory);
    return (error == cudaSuccess && freeMemory >= requiredMemory);
}

double CUDASSIMCalculator::calculateSSIMCUDA(const cv::Mat& img1, const cv::Mat& img2) {
    // Convert images to grayscale if needed
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

    // Ensure same size
    if (gray1.size() != gray2.size()) {
        cv::resize(gray2, gray2, gray1.size());
    }

    // Allocate buffers for this image size
    if (!allocateBuffers(gray1.cols, gray1.rows)) {
        return 0.0;
    }

    // Convert to float and upload to GPU
    cv::Mat float1, float2;
    gray1.convertTo(float1, CV_32F);
    gray2.convertTo(float2, CV_32F);

    if (!uploadImageToGPU(float1, m_gpuBuffer1) || !uploadImageToGPU(float2, m_gpuBuffer2)) {
        return 0.0;
    }

    // Launch SSIM kernel
    launchSSIMKernel(m_gpuBuffer1, m_gpuBuffer2, m_gpuResultBuffer,
                     gray1.cols, gray1.rows, m_stream);

    // Download result
    double result;
    if (!downloadResultFromGPU(m_gpuResultBuffer, result)) {
        return 0.0;
    }

    return result;
}

bool CUDASSIMCalculator::uploadImageToGPU(const cv::Mat& image, float* gpuBuffer) {
    size_t imageSize = image.rows * image.cols * sizeof(float);
    cudaError_t error = cudaMemcpyAsync(gpuBuffer, image.data, imageSize,
                                       cudaMemcpyHostToDevice, m_stream);
    return (error == cudaSuccess);
}

bool CUDASSIMCalculator::downloadResultFromGPU(float* gpuBuffer, double& result) {
    float floatResult;
    cudaError_t error = cudaMemcpyAsync(&floatResult, gpuBuffer, sizeof(float),
                                       cudaMemcpyDeviceToHost, m_stream);
    if (error != cudaSuccess) {
        return false;
    }

    cudaStreamSynchronize(m_stream);
    result = static_cast<double>(floatResult);
    return true;
}

#endif // CUDA_AVAILABLE

// ============================================================================
// OpenCL Implementation
// ============================================================================

#ifdef OPENCL_AVAILABLE

OpenCLSSIMCalculator::OpenCLSSIMCalculator() {
    if (!initializeOpenCL()) {
        m_errorMessage = "Failed to initialize OpenCL";
        m_isInitialized = false;
    }
}

OpenCLSSIMCalculator::~OpenCLSSIMCalculator() {
    cleanup();
}

bool OpenCLSSIMCalculator::initializeOpenCL() {
    try {
        cl_int error;

        // Get platform
        cl_uint platformCount;
        error = clGetPlatformIDs(0, nullptr, &platformCount);
        if (error != CL_SUCCESS || platformCount == 0) {
            m_errorMessage = "No OpenCL platforms found";
            return false;
        }

        std::vector<cl_platform_id> platforms(platformCount);
        error = clGetPlatformIDs(platformCount, platforms.data(), nullptr);
        if (error != CL_SUCCESS) {
            m_errorMessage = "Failed to get OpenCL platforms";
            return false;
        }

        m_platform = platforms[0];

        // Get device
        cl_uint deviceCount;
        error = clGetDeviceIDs(m_platform, CL_DEVICE_TYPE_GPU, 0, nullptr, &deviceCount);
        if (error != CL_SUCCESS || deviceCount == 0) {
            // Try CPU if no GPU available
            error = clGetDeviceIDs(m_platform, CL_DEVICE_TYPE_CPU, 0, nullptr, &deviceCount);
            if (error != CL_SUCCESS || deviceCount == 0) {
                m_errorMessage = "No OpenCL devices found";
                return false;
            }
        }

        std::vector<cl_device_id> devices(deviceCount);
        error = clGetDeviceIDs(m_platform, CL_DEVICE_TYPE_ALL, deviceCount, devices.data(), nullptr);
        if (error != CL_SUCCESS) {
            m_errorMessage = "Failed to get OpenCL devices";
            return false;
        }

        m_device = devices[0];

        // Get device name
        size_t nameSize;
        clGetDeviceInfo(m_device, CL_DEVICE_NAME, 0, nullptr, &nameSize);
        std::vector<char> name(nameSize);
        clGetDeviceInfo(m_device, CL_DEVICE_NAME, nameSize, name.data(), nullptr);
        m_deviceName = std::string(name.data());

        // Get device memory
        clGetDeviceInfo(m_device, CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(m_deviceMemoryTotal), &m_deviceMemoryTotal, nullptr);

        // Create context
        m_context = clCreateContext(nullptr, 1, &m_device, nullptr, nullptr, &error);
        if (error != CL_SUCCESS) {
            m_errorMessage = "Failed to create OpenCL context";
            return false;
        }

        // Create command queue
        m_commandQueue = clCreateCommandQueue(m_context, m_device, 0, &error);
        if (error != CL_SUCCESS) {
            m_errorMessage = "Failed to create OpenCL command queue";
            return false;
        }

        // Create kernels
        if (!createKernels()) {
            return false;
        }

        m_isInitialized = true;
        return true;

    } catch (const std::exception& e) {
        m_errorMessage = "OpenCL initialization exception: " + std::string(e.what());
        return false;
    }
}

void OpenCLSSIMCalculator::cleanup() {
    deallocateBuffers();

    if (m_ssimKernel) { clReleaseKernel(m_ssimKernel); m_ssimKernel = nullptr; }
    if (m_meanKernel) { clReleaseKernel(m_meanKernel); m_meanKernel = nullptr; }
    if (m_varianceKernel) { clReleaseKernel(m_varianceKernel); m_varianceKernel = nullptr; }
    if (m_covarianceKernel) { clReleaseKernel(m_covarianceKernel); m_covarianceKernel = nullptr; }
    if (m_program) { clReleaseProgram(m_program); m_program = nullptr; }
    if (m_commandQueue) { clReleaseCommandQueue(m_commandQueue); m_commandQueue = nullptr; }
    if (m_context) { clReleaseContext(m_context); m_context = nullptr; }

    m_isInitialized = false;
}

bool OpenCLSSIMCalculator::createKernels() {
    const char* kernelSource = getSSIMKernelSource();

    cl_int error;
    m_program = clCreateProgramWithSource(m_context, 1, &kernelSource, nullptr, &error);
    if (error != CL_SUCCESS) {
        m_errorMessage = "Failed to create OpenCL program";
        return false;
    }

    error = clBuildProgram(m_program, 1, &m_device, nullptr, nullptr, nullptr);
    if (error != CL_SUCCESS) {
        // Get build log
        size_t logSize;
        clGetProgramBuildInfo(m_program, m_device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &logSize);
        std::vector<char> log(logSize);
        clGetProgramBuildInfo(m_program, m_device, CL_PROGRAM_BUILD_LOG, logSize, log.data(), nullptr);
        m_errorMessage = "Failed to build OpenCL program: " + std::string(log.data());
        return false;
    }

    // Create kernels
    m_ssimKernel = clCreateKernel(m_program, "ssim_kernel", &error);
    if (error != CL_SUCCESS) {
        m_errorMessage = "Failed to create SSIM kernel";
        return false;
    }

    return true;
}

double OpenCLSSIMCalculator::calculateSSIM(const cv::Mat& img1, const cv::Mat& img2) {
    if (!m_isInitialized) {
        return 0.0;
    }

    return calculateSSIMOpenCL(img1, img2);
}

std::vector<double> OpenCLSSIMCalculator::calculateBatchSSIM(const std::vector<cv::Mat>& images) {
    std::vector<double> results;
    results.reserve(images.size() - 1);

    for (size_t i = 1; i < images.size(); ++i) {
        results.push_back(calculateSSIM(images[i-1], images[i]));
    }

    return results;
}

void OpenCLSSIMCalculator::warmUp(int warmupIterations) {
    if (!m_isInitialized) return;

    cv::Mat testImg1 = cv::Mat::ones(256, 256, CV_8UC1) * 128;
    cv::Mat testImg2 = cv::Mat::ones(256, 256, CV_8UC1) * 130;

    for (int i = 0; i < warmupIterations; ++i) {
        calculateSSIM(testImg1, testImg2);
    }

    clFinish(m_commandQueue);
}

std::string OpenCLSSIMCalculator::getDeviceInfo() const {
    return "OpenCL Device: " + m_deviceName +
           " (Memory: " + std::to_string(m_deviceMemoryTotal / (1024*1024)) + " MB)";
}

bool OpenCLSSIMCalculator::hasSufficientMemory(size_t requiredMemory) const {
    return m_deviceMemoryTotal >= requiredMemory;
}

double OpenCLSSIMCalculator::calculateSSIMOpenCL(const cv::Mat& img1, const cv::Mat& img2) {
    // Implementation similar to CUDA but using OpenCL APIs
    // This is a simplified version - full implementation would be more complex
    // Fallback to basic OpenCV implementation
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

    if (gray1.size() != gray2.size()) {
        cv::resize(gray2, gray2, gray1.size());
    }

    cv::Mat float1, float2;
    gray1.convertTo(float1, CV_64F);
    gray2.convertTo(float2, CV_64F);

    // Generic SSIM calculation
    cv::Scalar mean1 = cv::mean(float1);
    cv::Scalar mean2 = cv::mean(float2);

    double mu1 = mean1[0];
    double mu2 = mean2[0];

    cv::Mat img1_sq, img2_sq, img1_img2;
    cv::multiply(float1, float1, img1_sq);
    cv::multiply(float2, float2, img2_sq);
    cv::multiply(float1, float2, img1_img2);

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

bool OpenCLSSIMCalculator::allocateBuffers(int width, int height) {
    // OpenCL buffer allocation would be implemented here
    return true;
}

void OpenCLSSIMCalculator::deallocateBuffers() {
    // OpenCL buffer deallocation would be implemented here
}

const char* OpenCLSSIMCalculator::getSSIMKernelSource() {
    return R"(
__kernel void ssim_kernel(__global const float* img1,
                         __global const float* img2,
                         __global float* result,
                         int width, int height) {
    int gid = get_global_id(0);
    if (gid >= width * height) return;

    // Simplified SSIM calculation
    float diff = img1[gid] - img2[gid];
    result[gid] = diff * diff;
}
)";
}

#endif // OPENCL_AVAILABLE

// ============================================================================
// Metal Implementation (macOS/iOS)
// ============================================================================

#ifdef __APPLE__

MetalSSIMCalculator::MetalSSIMCalculator() {
    if (!initializeMetal()) {
        m_errorMessage = "Failed to initialize Metal";
        m_isInitialized = false;
    }
}

MetalSSIMCalculator::~MetalSSIMCalculator() {
    cleanup();
}

bool MetalSSIMCalculator::initializeMetal() {
    // Simplified Metal initialization without Objective-C
    // In a real implementation, this would need to be done in an Objective-C++ file
    m_deviceName = "Metal Device (Placeholder)";
    m_isInitialized = true;
    return true;
}

void MetalSSIMCalculator::cleanup() {
    deallocateBuffers();

    // Simplified cleanup without Objective-C
    // In a real implementation, this would need proper Metal resource cleanup
    m_ssimPipeline = nullptr;
    m_commandQueue = nullptr;
    m_device = nullptr;

    m_isInitialized = false;
}

bool MetalSSIMCalculator::createComputePipeline() {
    // Simplified Metal pipeline creation without Objective-C
    // In a real implementation, this would need to be done in an Objective-C++ file
    return true;
}

double MetalSSIMCalculator::calculateSSIM(const cv::Mat& img1, const cv::Mat& img2) {
    if (!m_isInitialized) {
        return 0.0;
    }

    return calculateSSIMMetal(img1, img2);
}

std::vector<double> MetalSSIMCalculator::calculateBatchSSIM(const std::vector<cv::Mat>& images) {
    std::vector<double> results;
    results.reserve(images.size() - 1);

    for (size_t i = 1; i < images.size(); ++i) {
        results.push_back(calculateSSIM(images[i-1], images[i]));
    }

    return results;
}

void MetalSSIMCalculator::warmUp(int warmupIterations) {
    if (!m_isInitialized) return;

    cv::Mat testImg1 = cv::Mat::ones(256, 256, CV_8UC1) * 128;
    cv::Mat testImg2 = cv::Mat::ones(256, 256, CV_8UC1) * 130;

    for (int i = 0; i < warmupIterations; ++i) {
        calculateSSIM(testImg1, testImg2);
    }
}

std::string MetalSSIMCalculator::getDeviceInfo() const {
    return "Metal Device: " + m_deviceName;
}

bool MetalSSIMCalculator::hasSufficientMemory(size_t requiredMemory) const {
    // Metal doesn't provide direct memory queries, assume sufficient for now
    return true;
}

double MetalSSIMCalculator::calculateSSIMMetal(const cv::Mat& img1, const cv::Mat& img2) {
    // Simplified implementation - would need full Metal compute shader implementation
    // For now, fall back to generic implementation
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

    if (gray1.size() != gray2.size()) {
        cv::resize(gray2, gray2, gray1.size());
    }

    cv::Mat float1, float2;
    gray1.convertTo(float1, CV_64F);
    gray2.convertTo(float2, CV_64F);

    // Generic SSIM calculation
    cv::Scalar mean1 = cv::mean(float1);
    cv::Scalar mean2 = cv::mean(float2);

    double mu1 = mean1[0];
    double mu2 = mean2[0];

    cv::Mat img1_sq, img2_sq, img1_img2;
    cv::multiply(float1, float1, img1_sq);
    cv::multiply(float2, float2, img2_sq);
    cv::multiply(float1, float2, img1_img2);

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

const char* MetalSSIMCalculator::getSSIMShaderSource() {
    return R"(
#include <metal_stdlib>
using namespace metal;

kernel void ssim_compute(device const float* img1 [[buffer(0)]],
                        device const float* img2 [[buffer(1)]],
                        device float* result [[buffer(2)]],
                        uint2 gid [[thread_position_in_grid]],
                        uint2 gridSize [[threads_per_grid]]) {
    uint index = gid.y * gridSize.x + gid.x;
    if (index >= gridSize.x * gridSize.y) return;

    // Simplified SSIM calculation
    float diff = img1[index] - img2[index];
    result[index] = diff * diff;
}
)";
}

bool MetalSSIMCalculator::allocateBuffers(int width, int height) {
    // Metal buffer allocation would be implemented here
    return true;
}

void MetalSSIMCalculator::deallocateBuffers() {
    // Metal buffer deallocation would be implemented here
}

#endif // __APPLE__

// ============================================================================
// DirectX Implementation (Windows)
// ============================================================================

#ifdef _WIN32

DirectXSSIMCalculator::DirectXSSIMCalculator() {
    if (!initializeDirectX()) {
        m_errorMessage = "Failed to initialize DirectX";
        m_isInitialized = false;
    }
}

DirectXSSIMCalculator::~DirectXSSIMCalculator() {
    cleanup();
}

bool DirectXSSIMCalculator::initializeDirectX() {
    try {
        HRESULT hr;

        // Create D3D11 device
        D3D_FEATURE_LEVEL featureLevel;
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            0,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            &m_device,
            &featureLevel,
            &m_deviceContext
        );

        if (FAILED(hr)) {
            m_errorMessage = "Failed to create D3D11 device";
            return false;
        }

        // Get device name
        IDXGIDevice* dxgiDevice = nullptr;
        hr = m_device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
        if (SUCCEEDED(hr)) {
            IDXGIAdapter* adapter = nullptr;
            hr = dxgiDevice->GetAdapter(&adapter);
            if (SUCCEEDED(hr)) {
                DXGI_ADAPTER_DESC desc;
                adapter->GetDesc(&desc);

                // Convert wide string to string
                char deviceName[256];
                WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, deviceName, sizeof(deviceName), nullptr, nullptr);
                m_deviceName = std::string(deviceName);

                m_deviceMemoryTotal = desc.DedicatedVideoMemory;

                adapter->Release();
            }
            dxgiDevice->Release();
        }

        // Create compute shader
        if (!createComputeShader()) {
            return false;
        }

        m_isInitialized = true;
        return true;

    } catch (const std::exception& e) {
        m_errorMessage = "DirectX initialization exception: " + std::string(e.what());
        return false;
    }
}

void DirectXSSIMCalculator::cleanup() {
    deallocateBuffers();

    if (m_ssimShader) { m_ssimShader->Release(); m_ssimShader = nullptr; }
    if (m_deviceContext) { m_deviceContext->Release(); m_deviceContext = nullptr; }
    if (m_device) { m_device->Release(); m_device = nullptr; }

    m_isInitialized = false;
}

bool DirectXSSIMCalculator::createComputeShader() {
    const char* shaderSource = getSSIMShaderSource();

    ID3DBlob* shaderBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;

    HRESULT hr = D3DCompile(
        shaderSource,
        strlen(shaderSource),
        nullptr,
        nullptr,
        nullptr,
        "CSMain",
        "cs_5_0",
        0,
        0,
        &shaderBlob,
        &errorBlob
    );

    if (FAILED(hr)) {
        if (errorBlob) {
            m_errorMessage = "Shader compilation failed: " + std::string((char*)errorBlob->GetBufferPointer());
            errorBlob->Release();
        } else {
            m_errorMessage = "Shader compilation failed";
        }
        return false;
    }

    hr = m_device->CreateComputeShader(
        shaderBlob->GetBufferPointer(),
        shaderBlob->GetBufferSize(),
        nullptr,
        &m_ssimShader
    );

    shaderBlob->Release();

    if (FAILED(hr)) {
        m_errorMessage = "Failed to create compute shader";
        return false;
    }

    return true;
}

double DirectXSSIMCalculator::calculateSSIM(const cv::Mat& img1, const cv::Mat& img2) {
    if (!m_isInitialized) {
        return 0.0;
    }

    return calculateSSIMDirectX(img1, img2);
}

std::vector<double> DirectXSSIMCalculator::calculateBatchSSIM(const std::vector<cv::Mat>& images) {
    std::vector<double> results;
    results.reserve(images.size() - 1);

    for (size_t i = 1; i < images.size(); ++i) {
        results.push_back(calculateSSIM(images[i-1], images[i]));
    }

    return results;
}

void DirectXSSIMCalculator::warmUp(int warmupIterations) {
    if (!m_isInitialized) return;

    cv::Mat testImg1 = cv::Mat::ones(256, 256, CV_8UC1) * 128;
    cv::Mat testImg2 = cv::Mat::ones(256, 256, CV_8UC1) * 130;

    for (int i = 0; i < warmupIterations; ++i) {
        calculateSSIM(testImg1, testImg2);
    }
}

std::string DirectXSSIMCalculator::getDeviceInfo() const {
    return "DirectX Device: " + m_deviceName +
           " (Memory: " + std::to_string(m_deviceMemoryTotal / (1024*1024)) + " MB)";
}

bool DirectXSSIMCalculator::hasSufficientMemory(size_t requiredMemory) const {
    return m_deviceMemoryTotal >= requiredMemory;
}

double DirectXSSIMCalculator::calculateSSIMDirectX(const cv::Mat& img1, const cv::Mat& img2) {
    // Simplified implementation - would need full DirectX compute shader implementation
    // For now, fall back to generic implementation
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

    if (gray1.size() != gray2.size()) {
        cv::resize(gray2, gray2, gray1.size());
    }

    cv::Mat float1, float2;
    gray1.convertTo(float1, CV_64F);
    gray2.convertTo(float2, CV_64F);

    // Generic SSIM calculation
    cv::Scalar mean1 = cv::mean(float1);
    cv::Scalar mean2 = cv::mean(float2);

    double mu1 = mean1[0];
    double mu2 = mean2[0];

    cv::Mat img1_sq, img2_sq, img1_img2;
    cv::multiply(float1, float1, img1_sq);
    cv::multiply(float2, float2, img2_sq);
    cv::multiply(float1, float2, img1_img2);

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

const char* DirectXSSIMCalculator::getSSIMShaderSource() {
    return R"(
[numthreads(16, 16, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    // DirectX compute shader for SSIM calculation
    // This is a simplified placeholder
}
)";
}

bool DirectXSSIMCalculator::allocateBuffers(int width, int height) {
    // DirectX buffer allocation would be implemented here
    return true;
}

void DirectXSSIMCalculator::deallocateBuffers() {
    // DirectX buffer deallocation would be implemented here
}

#endif // _WIN32

// ============================================================================
// GPUCalculatorFactory Implementation
// ============================================================================

std::unique_ptr<GPUSSIMCalculator> GPUCalculatorFactory::createBestGPUCalculator() {
    PlatformDetector& detector = PlatformDetector::getInstance();
    GPUAccelerationType bestType = detector.getBestGPUAcceleration();

    return createGPUCalculator(bestType);
}

std::unique_ptr<GPUSSIMCalculator> GPUCalculatorFactory::createGPUCalculator(GPUAccelerationType type) {
    switch (type) {
#ifdef CUDA_AVAILABLE
        case GPUAccelerationType::CUDA:
            if (isCUDAAvailable()) {
                return std::make_unique<CUDASSIMCalculator>();
            }
            break;
#endif

#ifdef OPENCL_AVAILABLE
        case GPUAccelerationType::OpenCL:
            if (isOpenCLAvailable()) {
                return std::make_unique<OpenCLSSIMCalculator>();
            }
            break;
#endif

#ifdef __APPLE__
        case GPUAccelerationType::Metal:
            if (isMetalAvailable()) {
                return std::make_unique<MetalSSIMCalculator>();
            }
            break;
#endif

#ifdef _WIN32
        case GPUAccelerationType::DirectX:
            if (isDirectXAvailable()) {
                return std::make_unique<DirectXSSIMCalculator>();
            }
            break;
#endif

        default:
            break;
    }

    return nullptr;
}

std::vector<GPUAccelerationType> GPUCalculatorFactory::getAvailableGPUTypes() {
    std::vector<GPUAccelerationType> types;

    if (isCUDAAvailable()) types.push_back(GPUAccelerationType::CUDA);
    if (isOpenCLAvailable()) types.push_back(GPUAccelerationType::OpenCL);
    if (isMetalAvailable()) types.push_back(GPUAccelerationType::Metal);
    if (isDirectXAvailable()) types.push_back(GPUAccelerationType::DirectX);
    if (isVulkanAvailable()) types.push_back(GPUAccelerationType::Vulkan);

    return types;
}

bool GPUCalculatorFactory::isGPUTypeAvailable(GPUAccelerationType type) {
    switch (type) {
        case GPUAccelerationType::CUDA: return isCUDAAvailable();
        case GPUAccelerationType::OpenCL: return isOpenCLAvailable();
        case GPUAccelerationType::Metal: return isMetalAvailable();
        case GPUAccelerationType::DirectX: return isDirectXAvailable();
        case GPUAccelerationType::Vulkan: return isVulkanAvailable();
        default: return false;
    }
}

bool GPUCalculatorFactory::isCUDAAvailable() {
#ifdef CUDA_AVAILABLE
    int deviceCount = 0;
    cudaError_t error = cudaGetDeviceCount(&deviceCount);
    return (error == cudaSuccess && deviceCount > 0);
#else
    return false;
#endif
}

bool GPUCalculatorFactory::isOpenCLAvailable() {
#ifdef OPENCL_AVAILABLE
    cl_uint platformCount = 0;
    cl_int error = clGetPlatformIDs(0, nullptr, &platformCount);
    return (error == CL_SUCCESS && platformCount > 0);
#else
    return false;
#endif
}

bool GPUCalculatorFactory::isMetalAvailable() {
#ifdef __APPLE__
    return true; // Metal is available on macOS 10.11+ and iOS 8+
#else
    return false;
#endif
}

bool GPUCalculatorFactory::isDirectXAvailable() {
#ifdef _WIN32
    ID3D11Device* device = nullptr;
    D3D_FEATURE_LEVEL featureLevel;

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &device,
        &featureLevel,
        nullptr
    );

    if (SUCCEEDED(hr) && device) {
        device->Release();
        return true;
    }
    return false;
#else
    return false;
#endif
}

bool GPUCalculatorFactory::isVulkanAvailable() {
    // Vulkan detection would be implemented here
    return false;
}