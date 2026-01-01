# AutoSlides Extractor - Cross-platform CMake Configuration
#
# This CMakeLists.txt is designed to work across different platforms:
# - macOS (Intel and Apple Silicon)
# - Linux (various distributions)
# - Windows (MSVC and MinGW)
#
# Prerequisites:
# - Qt6 (set CMAKE_PREFIX_PATH to Qt installation if not in standard location)
# - OpenCV 4.x
# - FFmpeg development libraries
# - CMake 3.16+
#
# Build instructions:
#   mkdir build && cd build
#   cmake ..
#   cmake --build . --config Release
#
# Environment variables that can be set:
#   CMAKE_PREFIX_PATH - Paths to Qt6, OpenCV, FFmpeg installations
#   PKG_CONFIG_PATH   - Paths to .pc files for pkg-config

cmake_minimum_required(VERSION 3.16)
project(AutoSlidesExtractor VERSION 1.1.0 LANGUAGES CXX)

# Enable resource compiler on Windows
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set default build type if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# Enable platform detection
include(CheckCXXCompilerFlag)
include(CheckIncludeFileCXX)
include(CheckLibraryExists)

# Platform Detection and Configuration
message(STATUS "Configuring for platform: ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}")

# Platform-specific configuration
if(APPLE)
    # Use system default compilers (Xcode command line tools)
    # CMAKE_C_COMPILER and CMAKE_CXX_COMPILER will be auto-detected

    # Enable modern C++ features on macOS
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")

    # Qt6 will be found via standard paths or CMAKE_PREFIX_PATH environment variable
    # Users can set: export CMAKE_PREFIX_PATH="/path/to/Qt/6.x.x/macos:$CMAKE_PREFIX_PATH"

elseif(WIN32)
    # Windows-specific configuration
    if(MSVC)
        # Enable modern C++ features and optimizations for MSVC
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++17 /O2")
        # Suppress some common MSVC warnings
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4996 /wd4267 /wd4244")
        # Prevent min/max macro conflicts with std::min/std::max
        add_definitions(-DNOMINMAX)
    endif()

elseif(UNIX AND NOT APPLE)
    # Linux-specific configuration
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # GCC-specific flags
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -Wall -Wextra")
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        # Clang-specific flags
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -Wall -Wextra")
    endif()
endif()

# Compiler-specific optimizations
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    message(STATUS "Building in Release mode with optimizations")
    if(MSVC)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /DNDEBUG")
    else()
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
    endif()
endif()

# Detect CPU architecture
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
    set(CPU_ARCH "ARM64")
    add_definitions(-DCPU_ARCH_ARM64)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(CPU_ARCH "x86_64")
    add_definitions(-DCPU_ARCH_X86_64)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "i386|i686")
    set(CPU_ARCH "x86")
    add_definitions(-DCPU_ARCH_X86)
else()
    set(CPU_ARCH "Unknown")
endif()

message(STATUS "Detected CPU architecture: ${CPU_ARCH}")

# SIMD Detection and Configuration
set(SIMD_FLAGS "")
set(SIMD_DEFINITIONS "")

if(CPU_ARCH STREQUAL "ARM64")
    # ARM64 NEON support (always available on ARM64)
    set(SIMD_FLAGS "${SIMD_FLAGS} -march=armv8-a")
    list(APPEND SIMD_DEFINITIONS "-DNEON_AVAILABLE")
    message(STATUS "Enabled NEON SIMD support")

elseif(CPU_ARCH STREQUAL "x86_64" OR CPU_ARCH STREQUAL "x86")
    # x86/x64 SIMD detection
    check_cxx_compiler_flag("-msse4.2" COMPILER_SUPPORTS_SSE42)
    check_cxx_compiler_flag("-mavx2" COMPILER_SUPPORTS_AVX2)
    check_cxx_compiler_flag("-mavx512f" COMPILER_SUPPORTS_AVX512)

    if(COMPILER_SUPPORTS_AVX512)
        set(SIMD_FLAGS "${SIMD_FLAGS} -mavx512f -mavx512bw -mavx512vl")
        list(APPEND SIMD_DEFINITIONS "-DAVX512_AVAILABLE")
        message(STATUS "Enabled AVX512 SIMD support")
    elseif(COMPILER_SUPPORTS_AVX2)
        set(SIMD_FLAGS "${SIMD_FLAGS} -mavx2")
        list(APPEND SIMD_DEFINITIONS "-DAVX2_AVAILABLE")
        message(STATUS "Enabled AVX2 SIMD support")
    elseif(COMPILER_SUPPORTS_SSE42)
        set(SIMD_FLAGS "${SIMD_FLAGS} -msse4.2")
        list(APPEND SIMD_DEFINITIONS "-DSSE42_AVAILABLE")
        message(STATUS "Enabled SSE4.2 SIMD support")
    endif()
endif()

# GPU Acceleration Detection
set(GPU_LIBRARIES "")
set(GPU_DEFINITIONS "")

# CUDA Detection (using modern CUDAToolkit)
find_package(CUDAToolkit QUIET)
if(CUDAToolkit_FOUND)
    set(GPU_DEFINITIONS "${GPU_DEFINITIONS};-DCUDA_AVAILABLE")
    list(APPEND GPU_LIBRARIES CUDA::cudart CUDA::cuda_driver)
    message(STATUS "CUDA found: ${CUDAToolkit_VERSION}")
else()
    message(STATUS "CUDA not found")
endif()

# OpenCL Detection
find_package(OpenCL QUIET)
if(OpenCL_FOUND)
    set(GPU_DEFINITIONS "${GPU_DEFINITIONS};-DOPENCL_AVAILABLE")
    list(APPEND GPU_LIBRARIES ${OpenCL_LIBRARIES})
    message(STATUS "OpenCL found")
else()
    message(STATUS "OpenCL not found")
endif()

# Metal Detection (macOS/iOS only)
if(APPLE)
    find_library(METAL_FRAMEWORK Metal)
    if(METAL_FRAMEWORK)
        set(GPU_DEFINITIONS "${GPU_DEFINITIONS};-DMETAL_AVAILABLE")
        list(APPEND GPU_LIBRARIES ${METAL_FRAMEWORK})
        message(STATUS "Metal framework found")
    endif()
endif()

# DirectX Detection (Windows only)
if(WIN32)
    find_library(D3D11_LIBRARY d3d11)
    find_library(DXGI_LIBRARY dxgi)
    if(D3D11_LIBRARY AND DXGI_LIBRARY)
        set(GPU_DEFINITIONS "${GPU_DEFINITIONS};-DDIRECTX_AVAILABLE")
        list(APPEND GPU_LIBRARIES ${D3D11_LIBRARY} ${DXGI_LIBRARY})
        message(STATUS "DirectX 11 found")
    endif()
endif()

# Vulkan Detection
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    set(GPU_DEFINITIONS "${GPU_DEFINITIONS};-DVULKAN_AVAILABLE")
    list(APPEND GPU_LIBRARIES ${Vulkan_LIBRARIES})
    message(STATUS "Vulkan found")
endif()

# Find required packages
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui)

# Find PkgConfig (optional on Windows, required on Unix)
if(WIN32)
    find_package(PkgConfig QUIET)
else()
    find_package(PkgConfig REQUIRED)
endif()

# Find OpenCV - only link required components to reduce bundle size
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)

if(OpenCV_FOUND)
    message(STATUS "OpenCV found: ${OpenCV_VERSION}")
    message(STATUS "OpenCV include dirs: ${OpenCV_INCLUDE_DIRS}")
    message(STATUS "OpenCV libraries: ${OpenCV_LIBS}")
else()
    message(FATAL_ERROR "OpenCV not found. Please install OpenCV or set OpenCV_DIR")
endif()

# Find ONNX Runtime (optional for ML classification)
# Priority: 1. Vendor binaries (with Core ML/CUDA/DirectML), 2. System installation, 3. Manual search
set(ONNX_DEFINITIONS "")
set(ONNX_LIBRARIES "")
set(ONNX_INCLUDE_DIRS "")
set(ONNX_FOUND FALSE)

# Step 1: Check vendor directory first (platform-specific with hardware acceleration)
message(STATUS "Searching for ONNX Runtime with hardware acceleration support...")

if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    set(VENDOR_ONNX_PATH "${CMAKE_SOURCE_DIR}/vendor/onnxruntime-osx-arm64-1.23.2")
    set(VENDOR_ONNX_LIB_NAME "libonnxruntime.1.23.2.dylib")
    set(VENDOR_ONNX_PROVIDER "Core ML")
elseif(APPLE)
    set(VENDOR_ONNX_PATH "${CMAKE_SOURCE_DIR}/vendor/onnxruntime-osx-x86_64-1.23.2")
    set(VENDOR_ONNX_LIB_NAME "libonnxruntime.1.23.2.dylib")
    set(VENDOR_ONNX_PROVIDER "Core ML")
elseif(WIN32)
    # Prefer GPU version (DirectML + CUDA) if available
    if(EXISTS "${CMAKE_SOURCE_DIR}/vendor/onnxruntime-win-x64-gpu-1.23.2")
        set(VENDOR_ONNX_PATH "${CMAKE_SOURCE_DIR}/vendor/onnxruntime-win-x64-gpu-1.23.2")
        set(VENDOR_ONNX_LIB_NAME "onnxruntime.lib")
        set(VENDOR_ONNX_PROVIDER "DirectML + CUDA")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/vendor/onnxruntime-win-x64-1.23.2")
        set(VENDOR_ONNX_PATH "${CMAKE_SOURCE_DIR}/vendor/onnxruntime-win-x64-1.23.2")
        set(VENDOR_ONNX_LIB_NAME "onnxruntime.lib")
        set(VENDOR_ONNX_PROVIDER "DirectML")
    endif()
elseif(UNIX)
    # Linux - check for CUDA version
    if(EXISTS "${CMAKE_SOURCE_DIR}/vendor/onnxruntime-linux-x64-gpu-1.23.2")
        set(VENDOR_ONNX_PATH "${CMAKE_SOURCE_DIR}/vendor/onnxruntime-linux-x64-gpu-1.23.2")
        set(VENDOR_ONNX_LIB_NAME "libonnxruntime.so.1.23.2")
        set(VENDOR_ONNX_PROVIDER "CUDA")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/vendor/onnxruntime-linux-x64-1.23.2")
        set(VENDOR_ONNX_PATH "${CMAKE_SOURCE_DIR}/vendor/onnxruntime-linux-x64-1.23.2")
        set(VENDOR_ONNX_LIB_NAME "libonnxruntime.so.1.23.2")
        set(VENDOR_ONNX_PROVIDER "CPU")
    endif()
endif()

# Check if vendor ONNX Runtime exists
if(VENDOR_ONNX_PATH AND EXISTS "${VENDOR_ONNX_PATH}")
    set(VENDOR_ONNX_INCLUDE "${VENDOR_ONNX_PATH}/include")
    set(VENDOR_ONNX_LIB "${VENDOR_ONNX_PATH}/lib/${VENDOR_ONNX_LIB_NAME}")

    if(EXISTS "${VENDOR_ONNX_INCLUDE}" AND EXISTS "${VENDOR_ONNX_LIB}")
        set(ONNX_FOUND TRUE)
        set(ONNX_DEFINITIONS "-DONNX_AVAILABLE")
        set(ONNX_INCLUDE_DIRS "${VENDOR_ONNX_INCLUDE}")
        set(ONNX_LIBRARIES "${VENDOR_ONNX_LIB}")
        message(STATUS "✓ Using vendor ONNX Runtime with ${VENDOR_ONNX_PROVIDER} support")
        message(STATUS "  Path: ${VENDOR_ONNX_PATH}")
        message(STATUS "  Include: ${ONNX_INCLUDE_DIRS}")
        message(STATUS "  Library: ${ONNX_LIBRARIES}")
    endif()
endif()

# Step 2: Try find_package if vendor not found
if(NOT ONNX_FOUND)
    find_package(onnxruntime QUIET)
    if(onnxruntime_FOUND)
        set(ONNX_FOUND TRUE)
        set(ONNX_DEFINITIONS "-DONNX_AVAILABLE")
        set(ONNX_LIBRARIES onnxruntime::onnxruntime)
        message(STATUS "ONNX Runtime found via find_package - ML classification enabled")
        message(STATUS "ONNX Runtime version: ${onnxruntime_VERSION}")
        message(STATUS "Note: System ONNX Runtime may not include hardware acceleration (Core ML/CUDA/DirectML)")
    endif()
endif()

# Step 3: Try manual search in common locations if still not found
if(NOT ONNX_FOUND)
    message(STATUS "ONNX Runtime not found via find_package, trying manual search...")

    set(ONNX_SEARCH_PATHS
        /usr/local/include
        /opt/homebrew/include
        /opt/homebrew/Cellar/onnxruntime/*/include
        /usr/include
        /opt/local/include
        C:/onnxruntime/include
        C:/vcpkg/installed/x64-windows/include
        ${CMAKE_PREFIX_PATH}/include
    )

    set(ONNX_LIB_SEARCH_PATHS
        /usr/local/lib
        /opt/homebrew/lib
        /opt/homebrew/Cellar/onnxruntime/*/lib
        /usr/lib
        /opt/local/lib
        C:/onnxruntime/lib
        C:/vcpkg/installed/x64-windows/lib
        ${CMAKE_PREFIX_PATH}/lib
    )

    find_path(ONNX_INCLUDE_DIR onnxruntime/onnxruntime_cxx_api.h
        PATHS ${ONNX_SEARCH_PATHS}
        NO_DEFAULT_PATH
    )

    # If found, adjust to parent directory
    if(ONNX_INCLUDE_DIR)
        get_filename_component(ONNX_INCLUDE_DIR "${ONNX_INCLUDE_DIR}" ABSOLUTE)
    endif()

    if(WIN32)
        find_library(ONNX_LIBRARY
            NAMES onnxruntime libonnxruntime onnxruntime.dll
            PATHS ${ONNX_LIB_SEARCH_PATHS}
            NO_DEFAULT_PATH
        )
    else()
        find_library(ONNX_LIBRARY
            NAMES onnxruntime libonnxruntime.dylib libonnxruntime.so
            PATHS ${ONNX_LIB_SEARCH_PATHS}
            NO_DEFAULT_PATH
        )
    endif()

    if(ONNX_INCLUDE_DIR AND ONNX_LIBRARY)
        set(ONNX_FOUND TRUE)
        set(ONNX_DEFINITIONS "-DONNX_AVAILABLE")
        set(ONNX_LIBRARIES ${ONNX_LIBRARY})
        set(ONNX_INCLUDE_DIRS ${ONNX_INCLUDE_DIR})
        message(STATUS "ONNX Runtime found via manual search - ML classification enabled")
        message(STATUS "  Include: ${ONNX_INCLUDE_DIR}")
        message(STATUS "  Library: ${ONNX_LIBRARY}")
        message(STATUS "Note: System ONNX Runtime may not include hardware acceleration (Core ML/CUDA/DirectML)")
    endif()
endif()

# Step 4: Report if not found
if(NOT ONNX_FOUND)
    message(STATUS "ONNX Runtime not found - ML classification will be disabled")
    message(STATUS "To enable ML classification with hardware acceleration:")
    message(STATUS "  1. Download pre-built binaries from:")
    message(STATUS "     https://github.com/microsoft/onnxruntime/releases/tag/v1.23.2")
    message(STATUS "  2. Extract to vendor/ directory:")
    message(STATUS "     - macOS ARM64: vendor/onnxruntime-osx-arm64-1.23.2/")
    message(STATUS "     - Windows: vendor/onnxruntime-win-x64-gpu-1.23.2/")
    message(STATUS "  3. Rebuild with CMake")
    message(STATUS "")
    message(STATUS "Alternative (without hardware acceleration):")
    message(STATUS "  macOS:   brew install onnxruntime")
    message(STATUS "  Windows: vcpkg install onnxruntime")
    message(STATUS "  Linux:   Build from source or use package manager")
endif()

# Find FFmpeg libraries using standard CMake approach
# Try to use PkgConfig first (most reliable on Unix systems)
if(PkgConfig_FOUND)
    pkg_check_modules(FFMPEG QUIET libavcodec libavformat libavutil libswscale)
endif()

if(FFMPEG_FOUND)
    message(STATUS "FFmpeg found via pkg-config")
    set(FFMPEG_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIRS})
    set(FFMPEG_LIBRARIES ${FFMPEG_LIBRARIES})
    set(FFMPEG_LIBRARY_DIRS ${FFMPEG_LIBRARY_DIRS})
else()
    # Fallback to manual search in common locations
    message(STATUS "FFmpeg not found via pkg-config, trying manual search...")

    # Common search paths for different platforms
    set(FFMPEG_SEARCH_PATHS
        /usr/local/include          # Linux/macOS Homebrew/MacPorts
        /opt/homebrew/include       # macOS Apple Silicon Homebrew
        /usr/include                # Linux system
        /opt/local/include          # MacPorts
        C:/ffmpeg/include           # Windows
        C:/vcpkg/installed/x64-windows/include  # Windows vcpkg
        ${CMAKE_PREFIX_PATH}/include
    )

    set(FFMPEG_LIB_SEARCH_PATHS
        /usr/local/lib
        /opt/homebrew/lib
        /usr/lib
        /opt/local/lib
        C:/ffmpeg/lib
        C:/vcpkg/installed/x64-windows/lib  # Windows vcpkg
        ${CMAKE_PREFIX_PATH}/lib
    )

    find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h
        PATHS ${FFMPEG_SEARCH_PATHS}
        PATH_SUFFIXES ffmpeg
    )

    # On Windows, libraries might have different names
    if(WIN32)
        find_library(AVCODEC_LIBRARY
            NAMES avcodec libavcodec avcodec-60 libavcodec-60
            PATHS ${FFMPEG_LIB_SEARCH_PATHS}
        )

        find_library(AVFORMAT_LIBRARY
            NAMES avformat libavformat avformat-60 libavformat-60
            PATHS ${FFMPEG_LIB_SEARCH_PATHS}
        )

        find_library(AVUTIL_LIBRARY
            NAMES avutil libavutil avutil-58 libavutil-58
            PATHS ${FFMPEG_LIB_SEARCH_PATHS}
        )

        find_library(SWSCALE_LIBRARY
            NAMES swscale libswscale swscale-7 libswscale-7
            PATHS ${FFMPEG_LIB_SEARCH_PATHS}
        )
    else()
        find_library(AVCODEC_LIBRARY avcodec
            PATHS ${FFMPEG_LIB_SEARCH_PATHS}
        )

        find_library(AVFORMAT_LIBRARY avformat
            PATHS ${FFMPEG_LIB_SEARCH_PATHS}
        )

        find_library(AVUTIL_LIBRARY avutil
            PATHS ${FFMPEG_LIB_SEARCH_PATHS}
        )

        find_library(SWSCALE_LIBRARY swscale
            PATHS ${FFMPEG_LIB_SEARCH_PATHS}
        )
    endif()

    # Check if all required libraries were found
    if(FFMPEG_INCLUDE_DIR AND AVCODEC_LIBRARY AND AVFORMAT_LIBRARY AND AVUTIL_LIBRARY AND SWSCALE_LIBRARY)
        set(FFMPEG_FOUND TRUE)
        set(FFMPEG_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIR})
        set(FFMPEG_LIBRARIES ${AVCODEC_LIBRARY} ${AVFORMAT_LIBRARY} ${AVUTIL_LIBRARY} ${SWSCALE_LIBRARY})
        message(STATUS "FFmpeg found via manual search")
    else()
        set(FFMPEG_FOUND FALSE)
        message(FATAL_ERROR "FFmpeg libraries not found. Please install FFmpeg development packages.")
    endif()
endif()

# VideoToolbox framework for hardware acceleration on macOS
if(APPLE)
    find_library(VIDEOTOOLBOX_FRAMEWORK VideoToolbox)
    find_library(COREMEDIA_FRAMEWORK CoreMedia)
    find_library(COREVIDEO_FRAMEWORK CoreVideo)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)
    find_library(ACCELERATE_FRAMEWORK Accelerate)
endif()

# Set up Qt
qt6_standard_project_setup()

# Source files
set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/settingsdialog.cpp
    src/rangeslider.cpp
    src/styledslider.cpp
    src/videoprocessor.cpp
    src/hardwaredecoder.cpp
    src/ssimcalculator.cpp
    src/slidedetector.cpp
    src/configmanager.cpp
    src/videoqueue.cpp
    src/processingthread.cpp
    src/chunkprocessor.cpp
    src/memoryoptimizer.cpp
    src/platformdetector.cpp
    src/optimizationmanager.cpp
    src/gpuacceleration.cpp
    src/performancemonitor.cpp
    src/phashcalculator.cpp
    src/trashmanager.cpp
    src/trashmetadata.cpp
    src/trashitemwidget.cpp
    src/trashreviewdialog.cpp
    src/postprocessor.cpp
    src/mlclassifier.cpp
    src/pdfmakerdialog.cpp
    src/slideitemwidget.cpp
)

# Add Windows resource file for icon
if(WIN32)
    list(APPEND SOURCES resources/AutoSlidesExtractor.rc)
endif()

# Header files
set(HEADERS
    src/mainwindow.h
    src/settingsdialog.h
    src/rangeslider.h
    src/styledslider.h
    src/videoprocessor.h
    src/hardwaredecoder.h
    src/ssimcalculator.h
    src/slidedetector.h
    src/configmanager.h
    src/videoqueue.h
    src/processingthread.h
    src/chunkprocessor.h
    src/memoryoptimizer.h
    src/platformdetector.h
    src/optimizationmanager.h
    src/gpuacceleration.h
    src/performancemonitor.h
    src/phashcalculator.h
    src/trashentry.h
    src/trashmanager.h
    src/trashmetadata.h
    src/trashitemwidget.h
    src/trashreviewdialog.h
    src/postprocessor.h
    src/mlclassifier.h
    src/pdfmakerdialog.h
    src/slideitemwidget.h
)

# Qt resources
qt_add_resources(RESOURCES resources.qrc)

# Create executable
qt6_add_executable(AutoSlidesExtractor ${SOURCES} ${HEADERS})

# Add resources to target
target_sources(AutoSlidesExtractor PRIVATE ${RESOURCES})

# Apply compiler flags and definitions
if(SIMD_FLAGS)
    set_target_properties(AutoSlidesExtractor PROPERTIES
        COMPILE_FLAGS "${SIMD_FLAGS}"
    )
endif()

if(SIMD_DEFINITIONS)
    target_compile_definitions(AutoSlidesExtractor PRIVATE ${SIMD_DEFINITIONS})
endif()

if(GPU_DEFINITIONS)
    target_compile_definitions(AutoSlidesExtractor PRIVATE ${GPU_DEFINITIONS})
endif()

if(ONNX_DEFINITIONS)
    target_compile_definitions(AutoSlidesExtractor PRIVATE ${ONNX_DEFINITIONS})
endif()

# Link libraries
target_link_libraries(AutoSlidesExtractor PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Gui
    ${OpenCV_LIBS}
    ${GPU_LIBRARIES}
)

# Link ONNX Runtime if available
if(ONNX_LIBRARIES)
    target_link_libraries(AutoSlidesExtractor PRIVATE ${ONNX_LIBRARIES})
endif()

# Link FFmpeg libraries
target_link_libraries(AutoSlidesExtractor PRIVATE
    ${FFMPEG_LIBRARIES}
)

# Add FFmpeg library directories if specified
if(FFMPEG_LIBRARY_DIRS)
    target_link_directories(AutoSlidesExtractor PRIVATE ${FFMPEG_LIBRARY_DIRS})
endif()

# Link platform-specific frameworks
if(APPLE)
    target_link_libraries(AutoSlidesExtractor PRIVATE
        ${VIDEOTOOLBOX_FRAMEWORK}
        ${COREMEDIA_FRAMEWORK}
        ${COREVIDEO_FRAMEWORK}
        ${COREFOUNDATION_FRAMEWORK}
        ${ACCELERATE_FRAMEWORK}
    )
endif()

# Include directories
target_include_directories(AutoSlidesExtractor PRIVATE
    src
    ${OpenCV_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIRS}
)

# Add ONNX Runtime include directories if available
if(ONNX_INCLUDE_DIRS)
    target_include_directories(AutoSlidesExtractor PRIVATE ${ONNX_INCLUDE_DIRS})
endif()

# Add GPU-specific include directories
if(CUDAToolkit_FOUND)
    target_include_directories(AutoSlidesExtractor PRIVATE ${CUDAToolkit_INCLUDE_DIRS})
endif()

if(OpenCL_FOUND)
    target_include_directories(AutoSlidesExtractor PRIVATE ${OpenCL_INCLUDE_DIRS})
endif()

if(Vulkan_FOUND)
    target_include_directories(AutoSlidesExtractor PRIVATE ${Vulkan_INCLUDE_DIRS})
endif()

# Set bundle properties for macOS
set_target_properties(AutoSlidesExtractor PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "AutoSlides Extractor"
    MACOSX_BUNDLE_DISPLAY_NAME "AutoSlides Extractor"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.autoslides.extractor"
    MACOSX_BUNDLE_ICON_FILE "AutoSlidesExtractor.icns"
    MACOSX_BUNDLE_INFO_STRING "AutoSlides Extractor - Extract slides from video presentations using advanced image processing"
    MACOSX_BUNDLE_LONG_VERSION_STRING "${PROJECT_VERSION} - Video Slide Extraction Tool"
    MACOSX_BUNDLE_COPYRIGHT "Copyright © 2025 AutoSlides Project. All rights reserved."
    WIN32_EXECUTABLE TRUE
)

# Copy icon file to app bundle on macOS
if(APPLE)
    set(MACOSX_BUNDLE_ICON_FILE "AutoSlidesExtractor.icns")
    set(APP_ICON_MACOSX "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.icns")
    set_source_files_properties(${APP_ICON_MACOSX} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(AutoSlidesExtractor PRIVATE ${APP_ICON_MACOSX})
endif()

# Windows icon is handled by the resource file (AutoSlidesExtractor.rc)
# which is already included in SOURCES for WIN32 builds


include(GNUInstallDirs)
install(TARGETS AutoSlidesExtractor
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
