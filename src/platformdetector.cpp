#include "platformdetector.h"
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>
#include <algorithm>

// Platform-specific includes
#ifdef _WIN32
    #include <windows.h>
    #include <intrin.h>
    #include <d3d11.h>
    #include <dxgi.h>
    #include <wbemidl.h>
    #include <comdef.h>
    #pragma comment(lib, "d3d11.lib")
    #pragma comment(lib, "dxgi.lib")
    #pragma comment(lib, "wbemuuid.lib")
#elif defined(__APPLE__)
    #include <sys/types.h>
    #include <sys/sysctl.h>
    #include <mach/mach.h>
    #include <mach/mach_host.h>
    #include <IOKit/IOKitLib.h>
    // #include <Metal/Metal.h>  // Causes C++ compilation issues
    #include <TargetConditionals.h>
#elif defined(__linux__)
    #include <sys/utsname.h>
    #include <unistd.h>
    #include <fstream>
    #include <cpuid.h>
    #include <sys/sysinfo.h>
#endif

// CUDA detection
#ifdef CUDA_AVAILABLE
    #include <cuda_runtime.h>
    #include <nvml.h>
#endif

// OpenCL detection
#ifdef OPENCL_AVAILABLE
    #ifdef __APPLE__
        #include <OpenCL/opencl.h>
    #else
        #include <CL/cl.h>
    #endif
#endif

// Vulkan detection
#ifdef VULKAN_AVAILABLE
    #include <vulkan/vulkan.h>
#endif

PlatformDetector& PlatformDetector::getInstance() {
    static PlatformDetector instance;
    return instance;
}

PlatformDetector::PlatformDetector() {
    initializePlatformInfo();
}

void PlatformDetector::initializePlatformInfo() {
    if (m_initialized) {
        return;
    }

    // Detect all platform capabilities
    detectOperatingSystem();
    detectCPUInfo();
    detectSIMDSupport();
    detectGPUAcceleration();
    detectMemoryInfo();
    detectCompilerInfo();

    m_initialized = true;
}

void PlatformDetector::detectOperatingSystem() {
#ifdef _WIN32
    m_platformInfo.operatingSystem = OperatingSystem::Windows;

    // Get Windows version
    OSVERSIONINFOEX osvi;
    ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);

    if (GetVersionEx((OSVERSIONINFO*)&osvi)) {
        std::ostringstream oss;
        oss << osvi.dwMajorVersion << "." << osvi.dwMinorVersion
            << " Build " << osvi.dwBuildNumber;
        m_platformInfo.osVersion = oss.str();
    }

#elif defined(__APPLE__)
    #if TARGET_OS_MAC
        m_platformInfo.operatingSystem = OperatingSystem::macOS;
    #elif TARGET_OS_IPHONE
        m_platformInfo.operatingSystem = OperatingSystem::iOS;
    #endif

    // Get macOS version
    size_t size = 0;
    sysctlbyname("kern.osrelease", nullptr, &size, nullptr, 0);
    if (size > 0) {
        std::vector<char> version(size);
        sysctlbyname("kern.osrelease", version.data(), &size, nullptr, 0);
        m_platformInfo.osVersion = std::string(version.data());
    }

#elif defined(__linux__)
    m_platformInfo.operatingSystem = OperatingSystem::Linux;

    // Get Linux version
    struct utsname unameData;
    if (uname(&unameData) == 0) {
        m_platformInfo.osVersion = std::string(unameData.release);
    }

#elif defined(__FreeBSD__)
    m_platformInfo.operatingSystem = OperatingSystem::FreeBSD;
#else
    m_platformInfo.operatingSystem = OperatingSystem::Unknown;
#endif
}

void PlatformDetector::detectCPUInfo() {
    // Detect CPU architecture
#if defined(__aarch64__) || defined(_M_ARM64)
    m_platformInfo.cpu.architecture = CPUArchitecture::ARM64;
#elif defined(__x86_64__) || defined(_M_X64)
    m_platformInfo.cpu.architecture = CPUArchitecture::x86_64;
#elif defined(__i386__) || defined(_M_IX86)
    m_platformInfo.cpu.architecture = CPUArchitecture::x86;
#elif defined(__riscv)
    m_platformInfo.cpu.architecture = CPUArchitecture::RISC_V;
#elif defined(__powerpc__) || defined(__ppc__)
    m_platformInfo.cpu.architecture = CPUArchitecture::PowerPC;
#else
    m_platformInfo.cpu.architecture = CPUArchitecture::Unknown;
#endif

    // Get thread count
    m_platformInfo.cpu.logicalCores = std::thread::hardware_concurrency();

    // Platform-specific CPU detection
    switch (m_platformInfo.operatingSystem) {
        case OperatingSystem::Windows:
            detectCPUInfo_Windows();
            break;
        case OperatingSystem::macOS:
        case OperatingSystem::iOS:
            detectCPUInfo_macOS();
            break;
        case OperatingSystem::Linux:
        case OperatingSystem::FreeBSD:
            detectCPUInfo_Linux();
            break;
        default:
            break;
    }
}

void PlatformDetector::detectCPUInfo_Windows() {
#ifdef _WIN32
    // Get CPU information using CPUID
    int cpuInfo[4] = {0};
    __cpuid(cpuInfo, 0);

    // Extract vendor string
    char vendor[13] = {0};
    memcpy(vendor, &cpuInfo[1], 4);
    memcpy(vendor + 4, &cpuInfo[3], 4);
    memcpy(vendor + 8, &cpuInfo[2], 4);
    m_platformInfo.cpu.vendor = std::string(vendor);

    // Get CPU model name
    __cpuid(cpuInfo, 0x80000000);
    if (cpuInfo[0] >= 0x80000004) {
        char modelName[49] = {0};
        __cpuid(cpuInfo, 0x80000002);
        memcpy(modelName, cpuInfo, 16);
        __cpuid(cpuInfo, 0x80000003);
        memcpy(modelName + 16, cpuInfo, 16);
        __cpuid(cpuInfo, 0x80000004);
        memcpy(modelName + 32, cpuInfo, 16);
        m_platformInfo.cpu.model = std::string(modelName);
    }

    // Get physical core count
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    m_platformInfo.cpu.physicalCores = sysInfo.dwNumberOfProcessors;

    // Try to get actual physical core count
    DWORD length = 0;
    GetLogicalProcessorInformation(nullptr, &length);
    if (length > 0) {
        std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buffer(
            length / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
        if (GetLogicalProcessorInformation(buffer.data(), &length)) {
            int physicalCores = 0;
            for (const auto& info : buffer) {
                if (info.Relationship == RelationProcessorCore) {
                    physicalCores++;
                }
            }
            if (physicalCores > 0) {
                m_platformInfo.cpu.physicalCores = physicalCores;
            }
        }
    }
#endif
}

void PlatformDetector::detectCPUInfo_macOS() {
#ifdef __APPLE__
    // Get CPU vendor and model
    size_t size = 0;

    // CPU brand string
    sysctlbyname("machdep.cpu.brand_string", nullptr, &size, nullptr, 0);
    if (size > 0) {
        std::vector<char> brand(size);
        sysctlbyname("machdep.cpu.brand_string", brand.data(), &size, nullptr, 0);
        m_platformInfo.cpu.model = std::string(brand.data());
    }

    // CPU vendor
    sysctlbyname("machdep.cpu.vendor", nullptr, &size, nullptr, 0);
    if (size > 0) {
        std::vector<char> vendor(size);
        sysctlbyname("machdep.cpu.vendor", vendor.data(), &size, nullptr, 0);
        m_platformInfo.cpu.vendor = std::string(vendor.data());
    }

    // Physical and logical core counts
    size = sizeof(int);
    sysctlbyname("hw.physicalcpu", &m_platformInfo.cpu.physicalCores, &size, nullptr, 0);
    sysctlbyname("hw.logicalcpu", &m_platformInfo.cpu.logicalCores, &size, nullptr, 0);

    // Cache line size
    sysctlbyname("hw.cachelinesize", &m_platformInfo.cpu.cacheLineSize, &size, nullptr, 0);

    // CPU frequencies (in Hz, convert to MHz)
    uint64_t freq = 0;
    size = sizeof(uint64_t);
    if (sysctlbyname("hw.cpufrequency", &freq, &size, nullptr, 0) == 0) {
        m_platformInfo.cpu.baseFrequency = static_cast<int>(freq / 1000000);
    }
    if (sysctlbyname("hw.cpufrequency_max", &freq, &size, nullptr, 0) == 0) {
        m_platformInfo.cpu.maxFrequency = static_cast<int>(freq / 1000000);
    }
#endif
}

void PlatformDetector::detectCPUInfo_Linux() {
#ifdef __linux__
    // Read CPU information from /proc/cpuinfo
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    int physicalCores = 0;

    while (std::getline(cpuinfo, line)) {
        if (line.find("model name") != std::string::npos) {
            size_t pos = line.find(':');
            if (pos != std::string::npos && m_platformInfo.cpu.model.empty()) {
                m_platformInfo.cpu.model = line.substr(pos + 2);
            }
        } else if (line.find("vendor_id") != std::string::npos) {
            size_t pos = line.find(':');
            if (pos != std::string::npos && m_platformInfo.cpu.vendor.empty()) {
                m_platformInfo.cpu.vendor = line.substr(pos + 2);
            }
        } else if (line.find("cpu cores") != std::string::npos) {
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                physicalCores = std::stoi(line.substr(pos + 2));
            }
        }
    }

    if (physicalCores > 0) {
        m_platformInfo.cpu.physicalCores = physicalCores;
    } else {
        m_platformInfo.cpu.physicalCores = m_platformInfo.cpu.logicalCores;
    }

    // Get cache line size
    std::ifstream cacheinfo("/sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size");
    if (cacheinfo.is_open()) {
        int cacheLineSize;
        cacheinfo >> cacheLineSize;
        if (cacheLineSize > 0) {
            m_platformInfo.cpu.cacheLineSize = cacheLineSize;
        }
    }
#endif
}

void PlatformDetector::detectSIMDSupport() {
    auto& simdSupport = m_platformInfo.cpu.supportedSIMD;

    if (m_platformInfo.cpu.architecture == CPUArchitecture::ARM64) {
        // ARM64 processors typically support NEON
        if (detectNEON()) {
            simdSupport.push_back(SIMDInstructionSet::NEON);
        }
    } else if (m_platformInfo.cpu.architecture == CPUArchitecture::x86_64 ||
               m_platformInfo.cpu.architecture == CPUArchitecture::x86) {
        // x86/x64 SIMD detection
        if (detectSSE()) simdSupport.push_back(SIMDInstructionSet::SSE);
        if (detectSSE2()) simdSupport.push_back(SIMDInstructionSet::SSE2);
        if (detectSSE3()) simdSupport.push_back(SIMDInstructionSet::SSE3);
        if (detectSSSE3()) simdSupport.push_back(SIMDInstructionSet::SSSE3);
        if (detectSSE4_1()) simdSupport.push_back(SIMDInstructionSet::SSE4_1);
        if (detectSSE4_2()) simdSupport.push_back(SIMDInstructionSet::SSE4_2);
        if (detectAVX()) simdSupport.push_back(SIMDInstructionSet::AVX);
        if (detectAVX2()) simdSupport.push_back(SIMDInstructionSet::AVX2);
        if (detectAVX512F()) simdSupport.push_back(SIMDInstructionSet::AVX512F);
        if (detectAVX512BW()) simdSupport.push_back(SIMDInstructionSet::AVX512BW);
        if (detectAVX512VL()) simdSupport.push_back(SIMDInstructionSet::AVX512VL);
    }

    if (simdSupport.empty()) {
        simdSupport.push_back(SIMDInstructionSet::None);
    }
}

bool PlatformDetector::detectNEON() {
#ifdef __aarch64__
    // NEON is mandatory on ARM64, so it's always available
    return true;
#elif defined(__ARM_NEON)
    return true;
#else
    return false;
#endif
}

bool PlatformDetector::detectSSE() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #ifdef _WIN32
        int cpuInfo[4];
        __cpuid(cpuInfo, 1);
        return (cpuInfo[3] & (1 << 25)) != 0;
    #elif defined(__GNUC__)
        unsigned int eax, ebx, ecx, edx;
        if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
            return (edx & (1 << 25)) != 0;
        }
    #endif
#endif
    return false;
}

bool PlatformDetector::detectSSE2() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #ifdef _WIN32
        int cpuInfo[4];
        __cpuid(cpuInfo, 1);
        return (cpuInfo[3] & (1 << 26)) != 0;
    #elif defined(__GNUC__)
        unsigned int eax, ebx, ecx, edx;
        if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
            return (edx & (1 << 26)) != 0;
        }
    #endif
#endif
    return false;
}

bool PlatformDetector::detectSSE3() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #ifdef _WIN32
        int cpuInfo[4];
        __cpuid(cpuInfo, 1);
        return (cpuInfo[2] & (1 << 0)) != 0;
    #elif defined(__GNUC__)
        unsigned int eax, ebx, ecx, edx;
        if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
            return (ecx & (1 << 0)) != 0;
        }
    #endif
#endif
    return false;
}

bool PlatformDetector::detectSSSE3() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #ifdef _WIN32
        int cpuInfo[4];
        __cpuid(cpuInfo, 1);
        return (cpuInfo[2] & (1 << 9)) != 0;
    #elif defined(__GNUC__)
        unsigned int eax, ebx, ecx, edx;
        if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
            return (ecx & (1 << 9)) != 0;
        }
    #endif
#endif
    return false;
}

bool PlatformDetector::detectSSE4_1() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #ifdef _WIN32
        int cpuInfo[4];
        __cpuid(cpuInfo, 1);
        return (cpuInfo[2] & (1 << 19)) != 0;
    #elif defined(__GNUC__)
        unsigned int eax, ebx, ecx, edx;
        if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
            return (ecx & (1 << 19)) != 0;
        }
    #endif
#endif
    return false;
}

bool PlatformDetector::detectSSE4_2() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #ifdef _WIN32
        int cpuInfo[4];
        __cpuid(cpuInfo, 1);
        return (cpuInfo[2] & (1 << 20)) != 0;
    #elif defined(__GNUC__)
        unsigned int eax, ebx, ecx, edx;
        if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
            return (ecx & (1 << 20)) != 0;
        }
    #endif
#endif
    return false;
}

bool PlatformDetector::detectAVX() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #ifdef _WIN32
        int cpuInfo[4];
        __cpuid(cpuInfo, 1);
        return (cpuInfo[2] & (1 << 28)) != 0;
    #elif defined(__GNUC__)
        unsigned int eax, ebx, ecx, edx;
        if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
            return (ecx & (1 << 28)) != 0;
        }
    #endif
#endif
    return false;
}

bool PlatformDetector::detectAVX2() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #ifdef _WIN32
        int cpuInfo[4];
        __cpuid(cpuInfo, 7);
        return (cpuInfo[1] & (1 << 5)) != 0;
    #elif defined(__GNUC__)
        unsigned int eax, ebx, ecx, edx;
        if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
            return (ebx & (1 << 5)) != 0;
        }
    #endif
#endif
    return false;
}

bool PlatformDetector::detectAVX512F() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #ifdef _WIN32
        int cpuInfo[4];
        __cpuid(cpuInfo, 7);
        return (cpuInfo[1] & (1 << 16)) != 0;
    #elif defined(__GNUC__)
        unsigned int eax, ebx, ecx, edx;
        if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
            return (ebx & (1 << 16)) != 0;
        }
    #endif
#endif
    return false;
}

bool PlatformDetector::detectAVX512BW() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #ifdef _WIN32
        int cpuInfo[4];
        __cpuid(cpuInfo, 7);
        return (cpuInfo[1] & (1 << 30)) != 0;
    #elif defined(__GNUC__)
        unsigned int eax, ebx, ecx, edx;
        if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
            return (ebx & (1 << 30)) != 0;
        }
    #endif
#endif
    return false;
}

bool PlatformDetector::detectAVX512VL() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #ifdef _WIN32
        int cpuInfo[4];
        __cpuid(cpuInfo, 7);
        return (cpuInfo[1] & (1 << 31)) != 0;
    #elif defined(__GNUC__)
        unsigned int eax, ebx, ecx, edx;
        if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
            return (ebx & (1 << 31)) != 0;
        }
    #endif
#endif
    return false;
}

void PlatformDetector::detectGPUAcceleration() {
    // Platform-specific GPU detection
    switch (m_platformInfo.operatingSystem) {
        case OperatingSystem::Windows:
            detectGPUInfo_Windows();
            break;
        case OperatingSystem::macOS:
        case OperatingSystem::iOS:
            detectGPUInfo_macOS();
            break;
        case OperatingSystem::Linux:
        case OperatingSystem::FreeBSD:
            detectGPUInfo_Linux();
            break;
        default:
            break;
    }

    // Cross-platform GPU API detection
    if (detectCUDA()) {
        GPUInfo cudaGPU;
        cudaGPU.accelerationType = GPUAccelerationType::CUDA;
        cudaGPU.name = "CUDA Device";
        m_platformInfo.gpus.push_back(cudaGPU);
    }

    if (detectOpenCL()) {
        GPUInfo openclGPU;
        openclGPU.accelerationType = GPUAccelerationType::OpenCL;
        openclGPU.name = "OpenCL Device";
        m_platformInfo.gpus.push_back(openclGPU);
    }

    if (detectVulkan()) {
        GPUInfo vulkanGPU;
        vulkanGPU.accelerationType = GPUAccelerationType::Vulkan;
        vulkanGPU.name = "Vulkan Device";
        m_platformInfo.gpus.push_back(vulkanGPU);
    }
}

void PlatformDetector::detectGPUInfo_Windows() {
#ifdef _WIN32
    // DirectX detection
    if (detectDirectX()) {
        GPUInfo dxGPU;
        dxGPU.accelerationType = GPUAccelerationType::DirectX;
        dxGPU.name = "DirectX Device";
        m_platformInfo.gpus.push_back(dxGPU);
    }
#endif
}

void PlatformDetector::detectGPUInfo_macOS() {
#ifdef __APPLE__
    // Metal detection
    if (detectMetal()) {
        GPUInfo metalGPU;
        metalGPU.accelerationType = GPUAccelerationType::Metal;
        metalGPU.name = "Metal Device";
        m_platformInfo.gpus.push_back(metalGPU);
    }
#endif
}

void PlatformDetector::detectGPUInfo_Linux() {
    // Linux-specific GPU detection will be handled by cross-platform APIs
    // (CUDA, OpenCL, Vulkan)
}

bool PlatformDetector::detectCUDA() {
#ifdef CUDA_AVAILABLE
    int deviceCount = 0;
    cudaError_t error = cudaGetDeviceCount(&deviceCount);
    return (error == cudaSuccess && deviceCount > 0);
#else
    return false;
#endif
}

bool PlatformDetector::detectOpenCL() {
#ifdef OPENCL_AVAILABLE
    cl_uint platformCount = 0;
    cl_int error = clGetPlatformIDs(0, nullptr, &platformCount);
    return (error == CL_SUCCESS && platformCount > 0);
#else
    return false;
#endif
}

bool PlatformDetector::detectMetal() {
#ifdef __APPLE__
    // Metal is available on macOS 10.11+ and iOS 8+
    return true;
#else
    return false;
#endif
}

bool PlatformDetector::detectDirectX() {
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

bool PlatformDetector::detectVulkan() {
#ifdef VULKAN_AVAILABLE
    VkInstance instance;
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
    if (result == VK_SUCCESS) {
        vkDestroyInstance(instance, nullptr);
        return true;
    }
    return false;
#else
    return false;
#endif
}

void PlatformDetector::detectMemoryInfo() {
    switch (m_platformInfo.operatingSystem) {
        case OperatingSystem::Windows:
            detectMemoryInfo_Windows();
            break;
        case OperatingSystem::macOS:
        case OperatingSystem::iOS:
            detectMemoryInfo_macOS();
            break;
        case OperatingSystem::Linux:
        case OperatingSystem::FreeBSD:
            detectMemoryInfo_Linux();
            break;
        default:
            break;
    }
}

void PlatformDetector::detectMemoryInfo_Windows() {
#ifdef _WIN32
    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus)) {
        m_platformInfo.memory.totalPhysicalMemory = memStatus.ullTotalPhys;
        m_platformInfo.memory.availablePhysicalMemory = memStatus.ullAvailPhys;
        m_platformInfo.memory.totalVirtualMemory = memStatus.ullTotalVirtual;
        m_platformInfo.memory.availableVirtualMemory = memStatus.ullAvailVirtual;
    }

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    m_platformInfo.memory.pageSize = sysInfo.dwPageSize;
#endif
}

void PlatformDetector::detectMemoryInfo_macOS() {
#ifdef __APPLE__
    // Get physical memory
    int64_t memSize = 0;
    size_t size = sizeof(memSize);
    if (sysctlbyname("hw.memsize", &memSize, &size, nullptr, 0) == 0) {
        m_platformInfo.memory.totalPhysicalMemory = static_cast<size_t>(memSize);
    }

    // Get available memory
    vm_size_t pageSize;
    vm_statistics64_data_t vmStats;
    mach_msg_type_number_t infoCount = sizeof(vmStats) / sizeof(natural_t);

    if (host_page_size(mach_host_self(), &pageSize) == KERN_SUCCESS &&
        host_statistics64(mach_host_self(), HOST_VM_INFO,
                         (host_info64_t)&vmStats, &infoCount) == KERN_SUCCESS) {
        m_platformInfo.memory.pageSize = pageSize;
        m_platformInfo.memory.availablePhysicalMemory =
            (vmStats.free_count + vmStats.inactive_count) * pageSize;
    }
#endif
}

void PlatformDetector::detectMemoryInfo_Linux() {
#ifdef __linux__
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        m_platformInfo.memory.totalPhysicalMemory = info.totalram * info.mem_unit;
        m_platformInfo.memory.availablePhysicalMemory = info.freeram * info.mem_unit;
        m_platformInfo.memory.totalVirtualMemory = info.totalswap * info.mem_unit;
        m_platformInfo.memory.availableVirtualMemory = info.freeswap * info.mem_unit;
    }

    m_platformInfo.memory.pageSize = getpagesize();
#endif
}

void PlatformDetector::detectCompilerInfo() {
#ifdef __clang__
    m_platformInfo.compilerName = "Clang";
    m_platformInfo.compilerVersion = __clang_version__;
#elif defined(__GNUC__)
    m_platformInfo.compilerName = "GCC";
    m_platformInfo.compilerVersion = __VERSION__;
#elif defined(_MSC_VER)
    m_platformInfo.compilerName = "MSVC";
    m_platformInfo.compilerVersion = std::to_string(_MSC_VER);
#else
    m_platformInfo.compilerName = "Unknown";
    m_platformInfo.compilerVersion = "Unknown";
#endif

#ifdef NDEBUG
    m_platformInfo.isDebugBuild = false;
#else
    m_platformInfo.isDebugBuild = true;
#endif

    m_platformInfo.buildTimestamp = __DATE__ " " __TIME__;
}

// Public interface methods
const PlatformInfo& PlatformDetector::getPlatformInfo() const {
    return m_platformInfo;
}

CPUArchitecture PlatformDetector::getCPUArchitecture() const {
    return m_platformInfo.cpu.architecture;
}

OperatingSystem PlatformDetector::getOperatingSystem() const {
    return m_platformInfo.operatingSystem;
}

const std::vector<SIMDInstructionSet>& PlatformDetector::getSupportedSIMD() const {
    return m_platformInfo.cpu.supportedSIMD;
}

bool PlatformDetector::isSIMDSupported(SIMDInstructionSet instructionSet) const {
    const auto& supported = m_platformInfo.cpu.supportedSIMD;
    return std::find(supported.begin(), supported.end(), instructionSet) != supported.end();
}

SIMDInstructionSet PlatformDetector::getBestSIMDInstructionSet() const {
    const auto& supported = m_platformInfo.cpu.supportedSIMD;

    // Priority order for x86/x64
    if (std::find(supported.begin(), supported.end(), SIMDInstructionSet::AVX512VL) != supported.end()) {
        return SIMDInstructionSet::AVX512VL;
    }
    if (std::find(supported.begin(), supported.end(), SIMDInstructionSet::AVX512BW) != supported.end()) {
        return SIMDInstructionSet::AVX512BW;
    }
    if (std::find(supported.begin(), supported.end(), SIMDInstructionSet::AVX512F) != supported.end()) {
        return SIMDInstructionSet::AVX512F;
    }
    if (std::find(supported.begin(), supported.end(), SIMDInstructionSet::AVX2) != supported.end()) {
        return SIMDInstructionSet::AVX2;
    }
    if (std::find(supported.begin(), supported.end(), SIMDInstructionSet::AVX) != supported.end()) {
        return SIMDInstructionSet::AVX;
    }
    if (std::find(supported.begin(), supported.end(), SIMDInstructionSet::SSE4_2) != supported.end()) {
        return SIMDInstructionSet::SSE4_2;
    }
    if (std::find(supported.begin(), supported.end(), SIMDInstructionSet::SSE4_1) != supported.end()) {
        return SIMDInstructionSet::SSE4_1;
    }

    // ARM SIMD
    if (std::find(supported.begin(), supported.end(), SIMDInstructionSet::NEON) != supported.end()) {
        return SIMDInstructionSet::NEON;
    }

    return SIMDInstructionSet::None;
}

std::vector<GPUAccelerationType> PlatformDetector::getAvailableGPUAcceleration() const {
    std::vector<GPUAccelerationType> types;
    for (const auto& gpu : m_platformInfo.gpus) {
        if (gpu.accelerationType != GPUAccelerationType::None) {
            types.push_back(gpu.accelerationType);
        }
    }
    return types;
}

bool PlatformDetector::isGPUAccelerationAvailable(GPUAccelerationType accelerationType) const {
    for (const auto& gpu : m_platformInfo.gpus) {
        if (gpu.accelerationType == accelerationType) {
            return true;
        }
    }
    return false;
}

GPUAccelerationType PlatformDetector::getBestGPUAcceleration() const {
    // Priority order for GPU acceleration
    if (isGPUAccelerationAvailable(GPUAccelerationType::CUDA)) {
        return GPUAccelerationType::CUDA;
    }
    if (isGPUAccelerationAvailable(GPUAccelerationType::Metal)) {
        return GPUAccelerationType::Metal;
    }
    if (isGPUAccelerationAvailable(GPUAccelerationType::DirectX)) {
        return GPUAccelerationType::DirectX;
    }
    if (isGPUAccelerationAvailable(GPUAccelerationType::Vulkan)) {
        return GPUAccelerationType::Vulkan;
    }
    if (isGPUAccelerationAvailable(GPUAccelerationType::OpenCL)) {
        return GPUAccelerationType::OpenCL;
    }

    return GPUAccelerationType::None;
}

const std::vector<GPUInfo>& PlatformDetector::getGPUInfo() const {
    return m_platformInfo.gpus;
}

const CPUInfo& PlatformDetector::getCPUInfo() const {
    return m_platformInfo.cpu;
}

const MemoryInfo& PlatformDetector::getMemoryInfo() const {
    return m_platformInfo.memory;
}

int PlatformDetector::getOptimalThreadCount(int reserveForSystem) const {
    int maxThreads = m_platformInfo.cpu.logicalCores;
    int optimalThreads = std::max(1, maxThreads - reserveForSystem);
    return optimalThreads;
}

bool PlatformDetector::supportsHardwareVideoDecoding() const {
    switch (m_platformInfo.operatingSystem) {
        case OperatingSystem::macOS:
        case OperatingSystem::iOS:
            return true; // VideoToolbox
        case OperatingSystem::Windows:
            return isGPUAccelerationAvailable(GPUAccelerationType::DirectX);
        case OperatingSystem::Linux:
            return isGPUAccelerationAvailable(GPUAccelerationType::CUDA) ||
                   isGPUAccelerationAvailable(GPUAccelerationType::OpenCL);
        default:
            return false;
    }
}

std::vector<std::string> PlatformDetector::getOptimizationFlags() const {
    std::vector<std::string> flags;

    // Architecture-specific flags
    if (m_platformInfo.cpu.architecture == CPUArchitecture::ARM64) {
        flags.push_back("-march=armv8-a");
        if (isSIMDSupported(SIMDInstructionSet::NEON)) {
            flags.push_back("-mfpu=neon");
        }
    } else if (m_platformInfo.cpu.architecture == CPUArchitecture::x86_64) {
        if (isSIMDSupported(SIMDInstructionSet::AVX512F)) {
            flags.push_back("-mavx512f");
        } else if (isSIMDSupported(SIMDInstructionSet::AVX2)) {
            flags.push_back("-mavx2");
        } else if (isSIMDSupported(SIMDInstructionSet::AVX)) {
            flags.push_back("-mavx");
        } else if (isSIMDSupported(SIMDInstructionSet::SSE4_2)) {
            flags.push_back("-msse4.2");
        }
    }

    // General optimization flags
    flags.push_back("-O3");
    flags.push_back("-DNDEBUG");
    flags.push_back("-ffast-math");

    return flags;
}

std::string PlatformDetector::getPlatformSummary() const {
    std::ostringstream oss;

    oss << "Platform Summary:\n";
    oss << "  OS: " << toString(m_platformInfo.operatingSystem)
        << " " << m_platformInfo.osVersion << "\n";
    oss << "  CPU: " << toString(m_platformInfo.cpu.architecture)
        << " (" << m_platformInfo.cpu.vendor << ")\n";
    oss << "  Model: " << m_platformInfo.cpu.model << "\n";
    oss << "  Cores: " << m_platformInfo.cpu.physicalCores
        << " physical, " << m_platformInfo.cpu.logicalCores << " logical\n";
    oss << "  Memory: " << (m_platformInfo.memory.totalPhysicalMemory / (1024*1024*1024))
        << " GB total, " << (m_platformInfo.memory.availablePhysicalMemory / (1024*1024*1024))
        << " GB available\n";

    oss << "  SIMD: ";
    for (const auto& simd : m_platformInfo.cpu.supportedSIMD) {
        oss << toString(simd) << " ";
    }
    oss << "\n";

    oss << "  GPU Acceleration: ";
    for (const auto& gpu : m_platformInfo.gpus) {
        oss << toString(gpu.accelerationType) << " ";
    }
    oss << "\n";

    oss << "  Compiler: " << m_platformInfo.compilerName
        << " " << m_platformInfo.compilerVersion << "\n";

    return oss.str();
}

void PlatformDetector::refreshPlatformInfo() {
    m_initialized = false;
    m_platformInfo = PlatformInfo{};
    initializePlatformInfo();
}

// String conversion methods
std::string PlatformDetector::toString(CPUArchitecture arch) {
    switch (arch) {
        case CPUArchitecture::ARM64: return "ARM64";
        case CPUArchitecture::x86_64: return "x86_64";
        case CPUArchitecture::x86: return "x86";
        case CPUArchitecture::RISC_V: return "RISC-V";
        case CPUArchitecture::PowerPC: return "PowerPC";
        default: return "Unknown";
    }
}

std::string PlatformDetector::toString(OperatingSystem os) {
    switch (os) {
        case OperatingSystem::macOS: return "macOS";
        case OperatingSystem::Windows: return "Windows";
        case OperatingSystem::Linux: return "Linux";
        case OperatingSystem::FreeBSD: return "FreeBSD";
        case OperatingSystem::Android: return "Android";
        case OperatingSystem::iOS: return "iOS";
        default: return "Unknown";
    }
}

std::string PlatformDetector::toString(SIMDInstructionSet simd) {
    switch (simd) {
        case SIMDInstructionSet::NEON: return "NEON";
        case SIMDInstructionSet::SSE: return "SSE";
        case SIMDInstructionSet::SSE2: return "SSE2";
        case SIMDInstructionSet::SSE3: return "SSE3";
        case SIMDInstructionSet::SSSE3: return "SSSE3";
        case SIMDInstructionSet::SSE4_1: return "SSE4.1";
        case SIMDInstructionSet::SSE4_2: return "SSE4.2";
        case SIMDInstructionSet::AVX: return "AVX";
        case SIMDInstructionSet::AVX2: return "AVX2";
        case SIMDInstructionSet::AVX512F: return "AVX512F";
        case SIMDInstructionSet::AVX512BW: return "AVX512BW";
        case SIMDInstructionSet::AVX512VL: return "AVX512VL";
        case SIMDInstructionSet::AltiVec: return "AltiVec";
        default: return "None";
    }
}

std::string PlatformDetector::toString(GPUAccelerationType gpu) {
    switch (gpu) {
        case GPUAccelerationType::CUDA: return "CUDA";
        case GPUAccelerationType::OpenCL: return "OpenCL";
        case GPUAccelerationType::Metal: return "Metal";
        case GPUAccelerationType::DirectX: return "DirectX";
        case GPUAccelerationType::Vulkan: return "Vulkan";
        case GPUAccelerationType::OpenGL: return "OpenGL";
        default: return "None";
    }
}