# Changelog

All notable changes to **AutoSlides Extractor** will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [1.1.0] - 2025-11-20

### 🚀 Added

#### AI & Machine Learning
- **ML Classification System**: Integrated MobileNetV4 ONNX model for intelligent slide classification.
  - Automatically identifies and removes:
    - `not_slide`: Desktop screens, black screens, no signal screens.
    - `may_be_slide`: Ambiguous content (configurable action).
- **Hardware Acceleration for AI**:
  - **macOS**: Core ML support (Apple Neural Engine).
  - **Windows**: CUDA (NVIDIA) and DirectML (AMD/Intel) support.
  - **Linux**: CUDA support.
- **Visual Confidence Controls**: New settings UI with dual-range sliders for tuning ML thresholds.

#### Trash Review System
- **Trash Review Dialog**: New dedicated interface for managing removed slides.
  - Grid view of all trashed items with metadata.
  - **Filtering**: Filter by source video or removal reason (pHash vs. ML vs. Manual).
  - **Restore**: Batch restore capabilities to return slides to their original folders.

#### PDF Export
- **PDF Maker**: New tool to compile extracted slides into PDF documents.
  - **Compression**: Optional JPEG compression (via libharu) to reduce file size.
  - **Resizing**: Presets for 1080p, 720p, etc., to optimize document size.
  - **Sorting**: Flexible sorting options (filename, date, custom drag-and-drop).

### 🛠 Changed
- **Settings UI**: Complete overhaul to accommodate new AI and PDF features.
- **Dependency Update**: Added ONNX Runtime and libharu dependencies.

---

## [1.0.1] - 2025-11-07

### 🚀 Added

#### Post-Processing System
- **Perceptual Hash (pHash)**: Implemented DCT-based pHash for robust duplicate detection (64-bit fingerprints).
- **Intelligent Deduplication**: Automatically cleans up redundant slides using Hamming distance comparison.
- **Exclusion Lists**: Match slides against blacklisted patterns (e.g., specific title slides).
- **Trash Manager**: Cross-platform safer deletion - moves files to system trash instead of permanent delete.

#### User Interface
- **Post-Processing Panel**: New split-screen layout with dedicated controls for post-processing.
- **Real-Time Stats**: Live tracking of "Saved" vs "Trashed" slides per video.
- **Settings Enhancements**: New tab for configuring exclusion lists and sensitivity thresholds.

### ⚙️ Technical
- **New Classes**: `PHashCalculator`, `PostProcessor`, `TrashManager`.
- **Persistence**: Enhanced `ConfigManager` to save/load complex exclusion rules.

---

## [1.0.0] - 2025-11-06

### 🎉 Initial Release

#### Core Features
- **Intelligent Detection**: Two-stage SSIM algorithm (change detection + stability verification).
- **High Performance**:
  - **Parallelism**: Multi-threaded processing pipeline.
  - **SIMD**: SSE4.2, AVX2, AVX512, NEON manual optimizations.
  - **GPU**: Hardware decoding via VideoToolbox, CUDA, D3D11, etc.
- **Smart Sampling**: I-frame based sampling for rapid analysis.

#### Compatibility
- **Cross-Platform**: Native builds for macOS (Apple Silicon), Windows (x64), and Linux.
- **Formats**: Full support for MP4, MKV, AVI, MOV, WMV.

