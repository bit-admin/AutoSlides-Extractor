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
project(AutoSlidesExtractor VERSION 1.0.1 LANGUAGES CXX)

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
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

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
    src/postprocessor.cpp
)

# Add Windows resource file for icon
if(WIN32)
    list(APPEND SOURCES resources/AutoSlidesExtractor.rc)
endif()

# Header files
set(HEADERS
    src/mainwindow.h
    src/settingsdialog.h
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
    src/trashmanager.h
    src/postprocessor.h
)

# Create executable
qt6_add_executable(AutoSlidesExtractor ${SOURCES} ${HEADERS})

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

# Link libraries
target_link_libraries(AutoSlidesExtractor PRIVATE
    Qt6::Core
    Qt6::Widgets
    ${OpenCV_LIBS}
    ${GPU_LIBRARIES}
)

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
