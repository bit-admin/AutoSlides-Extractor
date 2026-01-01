👉 [Chinese version for BIT user / 中文](./README_zh-CN.md)

<div align="center">
  <img src="resources/icon.png" alt="AutoSlides Extractor Logo" width="128" />
  <h1>AutoSlides Extractor</h1>
  <p><strong>Automatically extract slide images from video presentations using advanced computer vision</strong></p>

  <p>
    <a href="https://github.com/bit-admin/AutoSlides-Extractor/releases">
      <img src="https://img.shields.io/github/v/release/bit-admin/AutoSlides-Extractor?color=blue" alt="Version" />
    </a>
    <img src="https://img.shields.io/badge/platform-macOS%20%7C%20Windows%20%7C%20Linux-lightgrey?color=green" alt="Platform" />
    <img src="https://github.com/bit-admin/AutoSlides-Extractor/actions/workflows/build-windows.yml/badge.svg" alt="Github Action" />
  </p>
  <p>
    <img src="https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square&logo=c%2B%2B" alt="C++" />
    <img src="https://img.shields.io/badge/Qt-6-41CD52?style=flat-square&logo=qt" alt="Qt" />
    <img src="https://img.shields.io/badge/OpenCV-4.x-5C3EE8?style=flat-square&logo=opencv" alt="OpenCV" />
    <img src="https://img.shields.io/badge/FFmpeg-Enabled-007808?style=flat-square&logo=ffmpeg" alt="FFmpeg" />
  </p>

  <p>
    <a href="#-overview">Overview</a> •
    <a href="#-key-features">Features</a> •
    <a href="#-download--installation">Download</a> •
    <a href="#-quick-start">Quick Start</a> •
    <a href="#-how-it-works">How It Works</a> •
    <a href="#-building-from-source">Build</a>
  </p>
</div>

---

## Overview

**AutoSlides Extractor** is a powerful cross-platform desktop application designed to intelligently identify and extract distinct slides from presentation videos. Using sophisticated computer vision algorithms—specifically **Structural Similarity Index (SSIM)** and **Perceptual Hashing (pHash)**—it automatically detects when slide content changes significantly and saves each unique slide as a high-quality image.

**New in v1.1.0**: AI-powered slide classification using a **MobileNetV4 ONNX model** to automatically identify and remove non-slide content (desktop screens, black screens), a **trash review system**, and a **PDF Maker** for exporting slides.

## ✨ Key Features

| Core Capabilities | Advanced Tech |
|-------------------|---------------|
| 🎯 **Intelligent Slide Detection** <br> Two-stage algorithm using SSIM for precise slide extraction | ⚡ **Hardware Acceleration** <br> GPU support via CUDA, OpenCL, Metal, DirectX, and Vulkan |
| 🧹 **Smart Post-Processing** <br> Auto-removal of redundant slides using pHash & exclusion lists | 🤖 **AI-Powered Classification** <br> MobileNetV4 model filters out non-slide content |
| 🗑️ **Trash Review System** <br> Safely review and restore removed slides with metadata tracking | 🧩 **High Performance** <br> Multi-threading & SIMD optimizations (AVX2, NEON) |
| 📄 **PDF Maker** <br> Organize and export extracted slides to compressed PDF documents | 🎼 **Multi-Format Support** <br> MP4, AVI, MOV, MKV, WMV, FLV, WebM |

### Detailed Features
- **Configurable Sensitivity**: Presets (Strict, Normal, Loose) and custom SSIM thresholds.
- **Batch Processing**: Queue multiple videos for sequential processing.
- **Memory Optimization**: Chunk-based processing for handling large video files efficiently.
- **Cross-Platform**: Native look and feel on macOS, Windows, and Linux.

## 📥 Download & Installation

### Pre-built Releases

Download the latest installer or portable package for your operating system:

| Platform | Download | Note |
|----------|----------|------|
| **macOS** | **[AutoSlides.Extractor-macOS-arm64.dmg](https://github.com/bit-admin/AutoSlides-Extractor/releases)** | Apple Silicon only. Run quarantine fix command below. |
| **Windows** | **[Setup.exe](https://github.com/bit-admin/AutoSlides-Extractor/releases)** / **[Portable.zip](https://github.com/bit-admin/AutoSlides-Extractor/releases)** | 64-bit Windows 10 or later. |

> [!IMPORTANT]
> **macOS Users**: After installation, run the following command in Terminal to allow the app to run:
> ```bash
> sudo xattr -d com.apple.quarantine /Applications/AutoSlides\ Extractor.app
> ```

### System Requirements
* **OS**: macOS 11+ (Apple Silicon), Windows 10+ (64-bit)
* **CPU**: Quad-core processor recommended
* **RAM**: 8 GB minimum (16 GB for large videos)
* **GPU**: Recommended for hardware acceleration

## 🚀 Quick Start

1. **Launch** AutoSlides Extractor.
2. **Add Videos**: Drag & drop video files or click **"Add Videos"**.
3. **Configure** (Optional): Select an Output Directory and adjust SSIM sensitivity if needed.
4. **Start**: Click the **Start** button.
5. **Review**: Once finished, check the output folder. Use the **Trash Review** to recover any mistakenly removed slides or the **PDF Maker** to compile them.

## 🛠️ Usage Guide

### 1. Main Workflow
*   **Input**: Supports major video formats. Videos are queued and processed one by one.
*   **Monitoring**: Real-time status updates show:
    *   🟡 **Queued**: Waiting.
    *   🔵 **Processing**: FFmpeg decoding -> SSIM analysis -> ML Classification.
    *   🟢 **Completed**: Done with slide count.
*   **Controls**: Pause, Resume, or Remove items from queue easily.

### 2. Post-Processing & AI
*   **Redundancy Removal**: Uses pHash to find and delete near-duplicate images.
*   **Exclusion Lists**: Define patterns (like Intro/Outro slides) to automatically ignore.
*   **AI Classification (v1.1.0)**:
    *   **"slide"**: Kept.
    *   **"not_slide"**: Removed (e.g., desktop, black screen).
    *   **"may_be_slide"**: Configurable action (keep or delete).
    *   *Tip: Use the visual range sliders in Settings to tune confidence thresholds.*

### 3. PDF Export
*   Navigate to **PDF Maker**.
*   Select the root folder containing your extracted slides.
*   Choose sort order (Name/Date) and output quality (resize/compress).
*   Generate a single PDF document for your presentation.

## 🎯 How It Works

1.  **Stage 1: Change Detection (SSIM)**
    *   Samples frames (I-frames) and calculates structural similarity.
    *   Significant drops in similarity signal a potential new slide.
2.  **Stage 2: Stability Verification**
    *   Checks subsequent frames to ensure the "new slide" is stable and not just a transition effect.
3.  **Stage 3: Deduplication (pHash)**
    *   Computes perceptual hashes of extracted images.
    *   Removes duplicates even if minor pixel noise exists.
4.  **Stage 4: AI Filtering (MobileNetV4)**
    *   Classifies image content to remove non-slide elements like desktop wallpapers or empty screens.

## ⚙️ Technical Details

**Architecture**:
`Video Input` → `Hardware Decoder` → `SSIM Analysis` → `Image Output` → `pHash Deduplication` → `ML Classification` → `PDF Export`

**Performance**:
*   **SIMD**: SSE4.2, AVX2, AVX512, NEON.
*   **GPU Accel**: CUDA, DirectML, Metal, OpenCL.
*   **Inference**: Core ML (macOS), CUDA/DirectML (Windows).

## 🔧 Building from Source

**Prerequisites**:
*   **C++17 Compiler**, **CMake 3.16+**
*   **Qt 6** (Core, Widgets, Gui)
*   **OpenCV 4.x**, **FFmpeg**

**Build Steps**:
```bash
git clone https://github.com/bit-admin/AutoSlides-Extractor.git
cd AutoSlides-Extractor
mkdir build && cd build

# Configure
cmake ..
# Or with specific optimizations
# cmake -DENABLE_CUDA=ON ..

# Build
cmake --build . --config Release
```

## ❓ Troubleshooting

| Issue | Solution |
|-------|----------|
| **"App is damaged" on macOS** | Run `sudo xattr -d com.apple.quarantine /Applications/AutoSlides\ Extractor.app` in Terminal. |
| **GPU not used** | Ensure you have the latest drivers installed. On Windows, check if CUDA is installed for NVIDIA cards. |
| **Slides missed** | Try the **"Strict"** SSIM preset or decrease the custom threshold (e.g., to 0.995). |
| **Too many duplicates** | Enable **Post-Processing** and lower the **Hamming Threshold** (e.g., to 8). |

## 📄 License

This project is licensed under the [MIT License](LICENSE).

---

<p align="center">
  <em>Made with ❤️ using Qt6, OpenCV, and FFmpeg</em>
</p>