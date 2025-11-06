# Changelog

All notable changes to AutoSlides Extractor will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.1] - 2025-11-07

### Added

#### Post-Processing System
- **Perceptual Hash (pHash) Calculator**: New `PHashCalculator` class for computing perceptual hashes of images
  - Implements DCT-based perceptual hashing algorithm
  - Generates 64-bit hash fingerprints for image similarity detection
  - Supports Hamming distance calculation for hash comparison

- **Intelligent Post-Processor**: New `PostProcessor` class for automatic slide cleanup
  - Removes duplicate slides by comparing perceptual hashes
  - Matches slides against customizable exclusion list
  - Configurable Hamming distance threshold for similarity detection
  - Processes entire directories of extracted slides

- **Trash Management**: New `TrashManager` class for cross-platform file deletion
  - Moves unwanted slides to system trash/recycle bin instead of permanent deletion
  - Platform-specific implementations for macOS, Windows, and Linux
  - Safe file recovery option for users

#### User Interface Enhancements
- **Post-Processing Panel**: New right-side panel in main window
  - Enable/disable post-processing toggle
  - Delete redundant slides checkbox
  - Compare with exclusion list checkbox
  - Manual post-processing button for existing folders
  - Post-processing settings button

- **Post-Processing Results Table**: Real-time statistics display
  - Shows slides saved count per video
  - Shows slides moved to trash count per video
  - Updates automatically after each video processing

- **Enhanced Settings Dialog**: New post-processing configuration tab
  - Hamming threshold slider (0-20 range, default: 10)
  - Exclusion list management interface
  - Add/edit/remove exclusion entries with custom remarks
  - Import slide images to generate exclusion hashes
  - Default exclusion list with common unwanted patterns

#### Configuration Management
- **Post-Processing Settings Persistence**: Extended `ConfigManager` functionality
  - Save/load post-processing preferences
  - Save/load exclusion list with remarks
  - Default exclusion list on first run
  - Cross-platform settings storage (plist on macOS, registry on Windows)

### Changed

#### UI Layout
- **Main Window Layout**: Redesigned for better organization
  - Split into left and right panels (50/50 width distribution)
  - Left panel: Original controls (output directory, video input, queue, progress)
  - Right panel: New post-processing controls and results
  - Status log moved to full-width bottom section
  - Window default size increased from 480x600 to 960x600

#### Processing Workflow
- **Video Processing Pipeline**: Enhanced with post-processing stage
  - Automatic post-processing after slide extraction (if enabled)
  - Statistics tracking for moved/saved slides
  - Integration with video queue status updates

#### Settings Dialog
- **Tabbed Interface**: Reorganized settings into multiple tabs
  - Tab 0: SSIM and processing settings (existing)
  - Tab 1: Post-processing settings (new)
  - Programmatic tab selection support

### Technical Details

#### New Files
- `src/phashcalculator.h` / `src/phashcalculator.cpp` (166 lines)
- `src/postprocessor.h` / `src/postprocessor.cpp` (174 lines)
- `src/trashmanager.h` / `src/trashmanager.cpp` (31 lines)

#### Modified Files
- `src/mainwindow.h` / `src/mainwindow.cpp`: +355 lines (UI enhancements, post-processing integration)
- `src/settingsdialog.h` / `src/settingsdialog.cpp`: +350 lines (post-processing tab, exclusion list management)
- `src/configmanager.h` / `src/configmanager.cpp`: +79 lines (post-processing settings persistence)
- `src/videoqueue.h`: +7 lines (movedToTrash statistics tracking)
- `CMakeLists.txt`: Version bump to 1.0.1, added new source files
- `installer/AutoSlidesExtractor.nsi`: Version bump to 1.0.1.0

#### Default Exclusion List
The application now includes a default exclusion list with common unwanted slide patterns:
- Blank/white slides
- Title slides with "Thank You" text
- "Questions?" slides
- Common presentation templates

### Performance
- Post-processing adds minimal overhead (typically <1 second per video)
- pHash calculation is fast and memory-efficient
- Trash operations are non-blocking and platform-optimized

### Compatibility
- Maintains full backward compatibility with 1.0.0 configurations
- Existing users will see post-processing enabled by default with sensible defaults
- Settings migration is automatic and transparent

---

## [1.0.0] - 2025-11-06

### Initial Release

#### Core Features
- Intelligent slide detection using two-stage SSIM algorithm
- Hardware-accelerated video decoding (VideoToolbox, CUDA, etc.)
- Multi-format video support (MP4, AVI, MOV, MKV, WMV, FLV, WebM)
- Batch processing with queue management
- SIMD optimizations (SSE4.2, AVX2, AVX512, NEON)
- GPU acceleration support (CUDA, OpenCL, Metal, DirectX, Vulkan)
- Memory-optimized chunk processing for large videos
- Cross-platform support (macOS, Windows, Linux)

#### User Interface
- Qt6-based modern GUI
- Real-time progress tracking
- Configurable SSIM presets (Strict, Normal, Loose, Custom)
- Settings dialog for advanced configuration
- Status logging and monitoring

#### Technical Implementation
- Multi-threaded processing architecture
- Platform-specific optimizations
- Intelligent I-frame sampling
- Frame stability verification
- Configurable downsampling for anti-aliasing
