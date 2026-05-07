# Changelog

All notable changes to **AutoSlides Extractor** will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [1.2.0] - 2026-05-06

### 🚀 Added

#### Slides Review
- **Slides Review Dialog**: Replaced the old Trash Review workflow with a unified review interface for both extracted and removed slides.
  - Folder list now shows extracted and removed counts per slide folder.
  - Per-folder image grid interleaves extracted slides and removed slides in slide-index order.
  - Removed slides have category badges and red visual treatment; cropped extracted slides show a "Cropped" badge.
  - Added per-thumbnail **View** and **Set as Baseline** actions.
- **Full-Window Viewer**: Added a dedicated viewer page for inspecting slides from the review grid.
  - Extracted slides support crop, auto-crop, restore crop, and recrop actions.
  - Removed slides are shown read-only for inspection.
- **Review Filters and Batch Actions**:
  - Added Show filters for both/extracted/removed slides.
  - Added reason filters for pHash duplicate, pHash excluded, ML not-slide, ML maybe-slide, and manual removals.
  - Added selection helpers that operate only on currently visible filtered items.
  - Added batch restore for removed slides and batch delete for extracted slides.
  - Added folder-scoped empty-trash and delete-folder cleanup.

#### Non-Destructive Cropping
- **Crop Store**: Added `.extractorCrop/metadata.json` and original-image backups for non-destructive slide crops.
- **Manual Crop Workflow**: Users can draw a crop rectangle in image-pixel coordinates, apply it to the live slide image, and restore the original later.
- **Recrop Workflow**: Recropping starts from the original backup instead of the already-cropped image.
- **Baseline Crop Workflow**: A crop from one slide can be captured as a baseline and batch-applied proportionally to other slides.
- **Crop Cleanup**: Deleting a slide folder also cleans matching crop backups and metadata entries.

#### Auto Crop
- **Auto Crop Detector**: Added automatic slide-area detection using Canny edge detection and a YOLOv8 ONNX fallback.
- **Built-In YOLO Model**: Bundled `slide_detector_yolov8_v1.onnx` for AI-assisted crop detection.
- **Auto Crop Settings**: Added settings for detection mode, Canny aspect tolerance, YOLO confidence threshold, and custom YOLO model path.
- **Auto Crop Testing**: Added Settings UI to test Canny or YOLO detection on an image and preview the detected bounding box.
- **Batch Auto Crop**: Slides Review can auto-crop selected extracted slides and restore/crop selected removed `ml_maybe_slide` items when a slide area is detected.

#### Command-Line Interface
- **Direct CLI Launch**: Passing command-line arguments now runs the CLI directly without opening the main window; launching with no user arguments still opens the GUI.
- **SlidesExtractor Wrapper**: Added Settings UI to install, reinstall, uninstall, and inspect the command-line wrapper.
- **CLI Options**: Added command-line support for video/output paths, pHash duplicate removal, pHash exclusion matching, ML classification, threshold overrides, JPEG quality, and ad-hoc exclusion hashes.
- **Structured JSON Output**: Added `--json` mode with NDJSON events for start, video info, frame progress, trash events, ML state, post-processing progress, completion, errors, and cancellation.
- **Graceful CLI Cancellation**: Added SIGINT/SIGTERM and Windows console-control handling so active processing stops cleanly and returns conventional cancellation exit codes.
- **Unicode Arguments**: CLI mode uses Qt-decoded arguments so Unicode paths are preserved on Windows.

#### Slides Export
- **Batch PDF Export**: Slides Export can now create either one combined PDF or one PDF per selected folder.
- **Aspect Ratio Control**: Added 16:9 and 4:3 PDF output controls when reducing file size.
- **Output Folder Opening**: The Open button now opens either the generated PDF or the batch output folder.

### 🛠 Changed

- **Version Bump**: Updated application, CMake, Windows resource, NSIS installer, and vcpkg metadata from `1.1.0` to `1.2.0`.
- **Review Workflow Renaming**: Renamed **Review Trash** to **Slides Review** to reflect extracted-slide inspection, restoration, deletion, and cropping.
- **PDF Workflow Renaming**: Renamed **PDF Maker** to **Slides Export** and moved per-image inspection responsibilities to Slides Review.
- **Slides Export Refactor**: Simplified Slides Export to a folder-only workflow with ordering, selection, resize/compression controls, and PDF generation.
- **Processing Status**: Collapsed detailed queue states into a single `Processing` state for clearer queue behavior.
- **Trash Metadata**: Upgraded trash metadata to schema `1.1` with explicit removal categories and backward-compatible category derivation for older entries.
- **Status-Driven Dialog UX**: Removed QMessageBox usage from updated dialogs in favor of `statusMessage` signals.
- **macOS Bundle Icon**: Updated bundle icon metadata to use `icon.icns` consistently.
- **CLI Installer Detection**: Improved Windows wrapper freshness checks by parsing batch-file targets and comparing normalized executable paths.

### ⚙️ Technical

- **New Classes**: `AutoCropDetector`, `CropManager`, `CropMetadata`, `CropImageView`, `ReviewSlidesDialog`, `ReviewItemWidget`, `CliRunner`, and `CliInstaller`.
- **New Metadata Types**: Added `CropEntry` for crop backups and expanded `TrashEntry` with stable category keys.
- **Folder-Scoped Cleanup**: Added trash and crop cleanup helpers for deleting or emptying a single slide folder.

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
