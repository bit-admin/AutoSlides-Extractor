#ifndef PLATFORMDETECTOR_H
#define PLATFORMDETECTOR_H

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

/**
 * Enumeration of supported CPU architectures
 */
enum class CPUArchitecture {
    Unknown,
    ARM64,      // Apple Silicon, ARM64 processors
    x86_64,     // Intel/AMD 64-bit processors
    x86,        // 32-bit x86 processors
    RISC_V,     // RISC-V processors
    PowerPC     // PowerPC processors (legacy)
};

/**
 * Enumeration of supported operating systems
 */
enum class OperatingSystem {
    Unknown,
    macOS,
    Windows,
    Linux,
    FreeBSD,
    Android,
    iOS
};

/**
 * Enumeration of supported SIMD instruction sets
 */
enum class SIMDInstructionSet {
    None,
    // ARM SIMD
    NEON,
    // x86/x64 SIMD
    SSE,
    SSE2,
    SSE3,
    SSSE3,
    SSE4_1,
    SSE4_2,
    AVX,
    AVX2,
    AVX512F,
    AVX512BW,
    AVX512VL,
    // Other architectures
    AltiVec    // PowerPC
};

/**
 * Enumeration of supported GPU acceleration APIs
 */
enum class GPUAccelerationType {
    None,
    CUDA,       // NVIDIA CUDA
    OpenCL,     // Cross-platform OpenCL
    Metal,      // Apple Metal
    DirectX,    // Microsoft DirectX Compute Shaders
    Vulkan,     // Vulkan Compute
    OpenGL      // OpenGL Compute Shaders
};

/**
 * Structure containing detailed CPU information
 */
struct CPUInfo {
    CPUArchitecture architecture = CPUArchitecture::Unknown;
    std::string vendor;
    std::string model;
    int physicalCores = 0;
    int logicalCores = 0;
    int cacheLineSize = 64;  // Default cache line size
    std::vector<SIMDInstructionSet> supportedSIMD;

    // CPU features
    bool supportsHyperThreading = false;
    bool supportsVirtualization = false;

    // Frequency information (in MHz)
    int baseFrequency = 0;
    int maxFrequency = 0;
};

/**
 * Structure containing detailed GPU information
 */
struct GPUInfo {
    std::string name;
    std::string vendor;
    GPUAccelerationType accelerationType = GPUAccelerationType::None;
    size_t memorySize = 0;  // GPU memory in bytes
    int computeUnits = 0;   // Number of compute units/SMs
    std::string driverVersion;

    // API-specific information
    std::unordered_map<std::string, std::string> apiInfo;
};

/**
 * Structure containing system memory information
 */
struct MemoryInfo {
    size_t totalPhysicalMemory = 0;     // Total physical RAM in bytes
    size_t availablePhysicalMemory = 0; // Available physical RAM in bytes
    size_t totalVirtualMemory = 0;      // Total virtual memory in bytes
    size_t availableVirtualMemory = 0;  // Available virtual memory in bytes
    size_t pageSize = 4096;             // Memory page size in bytes

    // Memory features
    bool supportsLargePages = false;
    bool supportsNUMA = false;
};

/**
 * Structure containing complete platform information
 */
struct PlatformInfo {
    OperatingSystem operatingSystem = OperatingSystem::Unknown;
    std::string osVersion;
    CPUInfo cpu;
    std::vector<GPUInfo> gpus;
    MemoryInfo memory;

    // Compiler information
    std::string compilerName;
    std::string compilerVersion;

    // Runtime information
    bool isDebugBuild = false;
    std::string buildTimestamp;
};

/**
 * Cross-platform hardware and software detection system
 *
 * This class provides comprehensive detection of platform capabilities including:
 * - CPU architecture and SIMD instruction sets
 * - GPU acceleration capabilities
 * - Memory configuration
 * - Operating system features
 * - Compiler and build information
 */
class PlatformDetector {
public:
    /**
     * Get the singleton instance of PlatformDetector
     * @return Reference to the singleton instance
     */
    static PlatformDetector& getInstance();

    /**
     * Get complete platform information
     * @return PlatformInfo structure with all detected capabilities
     */
    const PlatformInfo& getPlatformInfo() const;

    /**
     * Get CPU architecture
     * @return Detected CPU architecture
     */
    CPUArchitecture getCPUArchitecture() const;

    /**
     * Get operating system
     * @return Detected operating system
     */
    OperatingSystem getOperatingSystem() const;

    /**
     * Get supported SIMD instruction sets
     * @return Vector of supported SIMD instruction sets
     */
    const std::vector<SIMDInstructionSet>& getSupportedSIMD() const;

    /**
     * Check if a specific SIMD instruction set is supported
     * @param instructionSet SIMD instruction set to check
     * @return true if supported
     */
    bool isSIMDSupported(SIMDInstructionSet instructionSet) const;

    /**
     * Get the best available SIMD instruction set for the platform
     * @return Best SIMD instruction set, or None if no SIMD support
     */
    SIMDInstructionSet getBestSIMDInstructionSet() const;

    /**
     * Get available GPU acceleration types
     * @return Vector of available GPU acceleration types
     */
    std::vector<GPUAccelerationType> getAvailableGPUAcceleration() const;

    /**
     * Check if a specific GPU acceleration type is available
     * @param accelerationType GPU acceleration type to check
     * @return true if available
     */
    bool isGPUAccelerationAvailable(GPUAccelerationType accelerationType) const;

    /**
     * Get the best available GPU acceleration type
     * @return Best GPU acceleration type, or None if no GPU acceleration
     */
    GPUAccelerationType getBestGPUAcceleration() const;

    /**
     * Get detailed GPU information
     * @return Vector of GPUInfo structures for all detected GPUs
     */
    const std::vector<GPUInfo>& getGPUInfo() const;

    /**
     * Get CPU information
     * @return CPUInfo structure with detailed CPU information
     */
    const CPUInfo& getCPUInfo() const;

    /**
     * Get memory information
     * @return MemoryInfo structure with memory configuration
     */
    const MemoryInfo& getMemoryInfo() const;

    /**
     * Get optimal number of threads for parallel processing
     * @param reserveForSystem Number of threads to reserve for system (default: 1)
     * @return Optimal thread count
     */
    int getOptimalThreadCount(int reserveForSystem = 1) const;

    /**
     * Check if the platform supports hardware-accelerated video decoding
     * @return true if hardware video decoding is available
     */
    bool supportsHardwareVideoDecoding() const;

    /**
     * Get platform-specific optimization flags for compilation
     * @return Vector of compiler optimization flags
     */
    std::vector<std::string> getOptimizationFlags() const;

    /**
     * Generate a human-readable platform summary
     * @return String containing platform information summary
     */
    std::string getPlatformSummary() const;

    /**
     * Force re-detection of platform capabilities
     * Useful if hardware configuration changes at runtime
     */
    void refreshPlatformInfo();

    /**
     * Convert enum values to strings for logging/debugging
     */
    static std::string toString(CPUArchitecture arch);
    static std::string toString(OperatingSystem os);
    static std::string toString(SIMDInstructionSet simd);
    static std::string toString(GPUAccelerationType gpu);

private:
    /**
     * Private constructor for singleton pattern
     */
    PlatformDetector();

    /**
     * Destructor
     */
    ~PlatformDetector() = default;

    /**
     * Deleted copy constructor and assignment operator
     */
    PlatformDetector(const PlatformDetector&) = delete;
    PlatformDetector& operator=(const PlatformDetector&) = delete;

    /**
     * Initialize platform detection
     */
    void initializePlatformInfo();

    /**
     * Detect CPU architecture and features
     */
    void detectCPUInfo();

    /**
     * Detect operating system information
     */
    void detectOperatingSystem();

    /**
     * Detect SIMD instruction set support
     */
    void detectSIMDSupport();

    /**
     * Detect GPU acceleration capabilities
     */
    void detectGPUAcceleration();

    /**
     * Detect memory configuration
     */
    void detectMemoryInfo();

    /**
     * Detect compiler information
     */
    void detectCompilerInfo();

    // Platform-specific detection methods
    void detectCPUInfo_Windows();
    void detectCPUInfo_macOS();
    void detectCPUInfo_Linux();

    void detectGPUInfo_Windows();
    void detectGPUInfo_macOS();
    void detectGPUInfo_Linux();

    void detectMemoryInfo_Windows();
    void detectMemoryInfo_macOS();
    void detectMemoryInfo_Linux();

    // SIMD detection helpers
    bool detectNEON();
    bool detectSSE();
    bool detectSSE2();
    bool detectSSE3();
    bool detectSSSE3();
    bool detectSSE4_1();
    bool detectSSE4_2();
    bool detectAVX();
    bool detectAVX2();
    bool detectAVX512F();
    bool detectAVX512BW();
    bool detectAVX512VL();

    // GPU detection helpers
    bool detectCUDA();
    bool detectOpenCL();
    bool detectMetal();
    bool detectDirectX();
    bool detectVulkan();

    /**
     * Platform information storage
     */
    PlatformInfo m_platformInfo;
    bool m_initialized = false;
};

#endif // PLATFORMDETECTOR_H