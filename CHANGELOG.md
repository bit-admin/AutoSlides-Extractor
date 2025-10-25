# Changelog

All notable changes to **AutoSlides Extractor** will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [2.0.0] - 2026-08-08

### 🚀 Added

#### Timeline (`timeline.json`)
- **Media-time → slide map**: Each slides folder can now contain a colocated `timeline.json` that records I-frame PTS media times for confirmed captures, so players (including Electron consumers) can map video position to the current slide image.
- **`TimelineMetadata`**: New writer/manager (schema v1, `extractor: "qt"`) with capture events (`changeAt` / `confirmedAt` / `initialFile`) and a mutable `resolutions` map (`canonical` | `duplicate` | `gap`). Events are never deleted; post-process and Review only update resolutions.
- **Capture path**: Hardware decode now carries media PTS seconds per sampled I-frame through `FrameChunk` into `SlideDetector`, which returns `ConfirmedSlideTiming` alongside selected indices. On successful save, `ProcessingThread` appends a capture event when enabled.
- **Resolution updates**:
  - pHash duplicate / post-crop duplicate → `duplicate` (`duplicateOf` first-kept basename)
  - Exclusion list / ML trash → `gap` (`exclusion` / `ai_filtered`)
  - Slides Review manual delete → `gap` (`manual_trash`); restore and auto-crop-from-trash restore → `canonical`
- **Settings**: GUI default **on** under Processing → Output Settings (“Write timeline.json”). CLI **off** unless **`--write-timeline`** (does not inherit GUI true). Compatible with `--compatible`.

#### Post-process auto-crop for ML `may_be_slide`
- **Keep instead of trash**: When ML would delete a `may_be_slide_*` frame, optionally run the same Canny/YOLO `AutoCropDetector` used by Review and commit an in-place crop via `CropManager::applyCrop(..., autoCropped=true)`. Success keeps the live slide; failure falls back to trash as `ml_maybe_slide`.
- **Post-crop pHash**: After successful auto-crops, a candidate-only pHash pass rehashes those files, seeds “seen” from remaining non-candidate slides, and trashes post-crop duplicates as `phash_duplicate` (`.extractorCrop` backup/metadata left intact for Review restore).
- **Settings UI**: Nested under Delete may_be_slide in ML Classification:
  - “Auto-crop may_be_slide before delete (keep if crop succeeds)” (`mlAutoCropMaybeSlides`, GUI default **true**)
  - “Re-check pHash duplicates after auto-crop” (`mlPostCropDedup`, GUI default **true**; enabled only when Delete + Auto-crop are on)
- **CLI opt-in** (forced false unless flags set; never inherit GUI true):
  - **`--ml-autocrop-maybe`**
  - **`--ml-postcrop-dedup`** (no-op without auto-crop)
- **Crop metadata 1.1**: `CropEntry` / `.extractorCrop/metadata.json` gain `autoCropped` bool. Load accepts schema 1.0 and 1.1; missing key defaults to false. Manual Review crops remain `autoCropped=false`.

#### Command-Line Interface
- **`--write-timeline`**: Opt-in media-time map for headless / Electron-compatible runs.
- **`--ml-autocrop-maybe`** / **`--ml-postcrop-dedup`**: Opt-in post-process auto-crop path (forbidden under `--compatible` like other post-process flags).

### 🛠 Changed

- **Version Bump**: Application, CMake project, Windows resource, NSIS installer, and vcpkg metadata updated from `1.2.2` to `2.0.0`.
- **Detection pipeline timing**: `HardwareDecoder` chunk callbacks, `FrameChunk`, `ProcessingState` (pending change PTS across chunks), and `SlideDetector::detectSlidesFromChunk` now thread media timestamps through confirmation so timeline capture is accurate across chunk boundaries.
- **`PostProcessor` / `ProcessingPipeline`**: `processDirectory` extended with auto-crop flags, `AutoCropConfig`, and JPEG quality; result counters add `autoCroppedKept` and `removedByPostCropPHash`.
- **Windows CI packaging**: Rewrote `.github/workflows/build-windows.yml` for portable ZIP + NSIS with concurrency cancel, pinned vcpkg commit + binary cache (not full installed tree), DirectML/CPU ONNX package (no CUDA redistributable), dep discovery from the built binary (no hardcoded OpenCV 4.x names), fail-hard missing DLLs, version from `CMakeLists.txt`, and `--version`-only verification.

### ⚙️ Technical

- **New class**: `TimelineMetadata` (`src/timelinemetadata.h` / `.cpp`).
- **New types**: `SlideCaptureEvent`, `SlideResolution`, `TimelineData`, `ConfirmedSlideTiming`.
- **Config keys**: `writeTimeline`, `mlAutoCropMaybeSlides`, `mlPostCropDedup`.
- **OpenCV 5**: `autocropdetector.cpp` includes `opencv2/geometry.hpp` for contour helpers moved out of the main OpenCV header.
- **mark\* no-ops**: Timeline resolution helpers are silent when `timeline.json` is absent so post-process/Review stay unchanged when timeline writing is off.

---

## [1.2.2] - 2026-06-06

### 🛠 Changed

#### Structural Refactor
- **Dead Code Removal**: Deleted the entire GPU-SSIM optimization layer (~5,166 lines) — `OptimizationManager`, `GPUAcceleration`, `PlatformDetector`, and `PerformanceMonitor` were never instantiated or called. The CUDA/OpenCL/Metal/DirectX SSIM backends were incomplete stubs (empty `createGPUCalculators()`, undefined CUDA kernel, wrong OpenCL math, placeholder Metal/DirectX implementations). Removed the associated CMake GPU-detection machinery. The live GPU paths (FFmpeg hardware decoding, ONNX Runtime inference) are unaffected.
- **ProcessingPipeline Extraction**: Centralized the 16-argument `PostProcessor::processDirectory(...)` call and signal wiring — previously hand-copied in four places (`MainWindow` ×2, `CliRunner`, `ReviewSlidesDialog`) — into a single `ProcessingPipeline` class with a `Request` struct and stable signal surface.
- **PdfExporter Extraction**: Moved the 100-line PDF rendering lambda from `PdfMakerDialog::onMakePdf` into a standalone `PdfExporter` class. The dialog now flattens selected folders into an ordered image list and delegates rendering.
- **NaturalSorter Extraction**: Deduplicated the byte-identical natural-order tokenizer and comparator (~110 lines each) from `PdfMakerDialog` and `ReviewSlidesDialog` into a shared `NaturalSorter` namespace with `tokenize`, `lessThan`, and `sort` helpers.
- **ReviewSlidesDialog Metadata-Reload Deduplication**: Replaced seven identical 13-line metadata-reload blocks and ten folder-name fallback usages with three private helpers: `folderNameForEntry`, `rebuildCurrentFolderRemoved`, and `reloadCurrentFolderItems`.
- **CropManager::scaleCropRect**: Moved the proportional crop-rect scaling and clamping math (used by baseline crop) from `ReviewSlidesDialog::onApplyBaseline` into `CropManager::scaleCropRect`, a pure geometry helper alongside `applyCrop` and `restoreCrop`.
- **MLClassifier::explainDecision**: Moved the 80+ lines of 2-stage ML decision reasoning from `SettingsDialog::onTestMLClassificationClicked` into `MLClassifier::explainDecision`, which returns the verdict plus per-stage reason lines — a single source of truth instead of duplicated logic.
- **SettingsDialog Decomposition**: Reduced `SettingsDialog` from 1,663 lines to 200 lines (.cpp) and 177 to 94 lines (.h) by extracting five self-contained tab widgets and a preview widget:
  - `ProcessingTab` — SSIM preset, downsampling, chunk size, JPEG quality.
  - `PostProcessingTab` — Hamming threshold and pHash exclusion list management.
  - `MLClassificationTab` — Model path, 2-stage threshold sliders, single-image test panel.
  - `AutoCropTab` — Detection mode, Canny/YOLO parameters, interactive test panel.
  - `CliTab` — CLI wrapper install/uninstall and usage examples.
  - `AutoCropTestPreviewWidget` — Inline 170-line preview class extracted to its own files.
  - `SettingsDialog` is now a thin coordinator: creates tabs, wires `statusMessage` signals, delegates `load`/`store`/reset.
- **MainWindow::updateQueueTable Split**: Decomposed the 116-line method into a thin loop plus `populateQueueRow` (single-row cell fill) and `rowColorForStatus` (theme-aware status colour).
- **SettingsDialog AutoCropTab**: Moved auto-crop test preview widget and run-test logic into a self-contained tab widget.
- **SettingsDialog CliTab**: Moved CLI install/uninstall slots and usage examples into a self-contained tab widget.

### ⚙️ Technical

- **New Shared Classes**: `ProcessingPipeline`, `PdfExporter`, `NaturalSorter`, `CropManager::scaleCropRect`, `MLClassifier::explainDecision`.
- **New UI Widgets**: `ProcessingTab`, `PostProcessingTab`, `MLClassificationTab`, `AutoCropTab`, `CliTab`, `AutoCropTestPreviewWidget`.
- **Deleted Classes**: `OptimizationManager`, `GPUAcceleration`, `PlatformDetector`, `PerformanceMonitor`.
- **Codebase Metrics**: Net −5,100 lines (41 files changed, +2,549 / −7,640). `src/` total reduced from 25,320 to 20,297 lines (−20%). All changes are behavior-preserving.

---

## [1.2.1] - 2026-05-09

### 🚀 Added

#### Command-Line Interface
- **Compatibility Mode**: Added `--compatible` for external Electron-style consumers.
  - Strips the `screen_` prefix from generated folder and slide names.
  - Outputs slides as `Slide_<video>_<index>.png` instead of JPEG files.
  - Skips post-processing and disallows pHash/ML options that conflict with compatibility output.
  - Includes a `compatible` flag in JSON start events.

### 🛠 Changed

- **Version Bump**: Updated application, CMake, Windows resource, NSIS installer, and vcpkg metadata from `1.2.0` to `1.2.1`.

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
