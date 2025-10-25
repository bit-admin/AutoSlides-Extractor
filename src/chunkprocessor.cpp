#include "chunkprocessor.h"

/**
 * Implementation file for chunk processor data structures
 *
 * This file provides the implementation for the chunk-based processing system
 * used in the AutoSlides Extractor application. The system is designed to
 * minimize memory usage while maintaining the accuracy of the slide detection
 * algorithm through a simplified single-frame overlap mechanism.
 *
 * Key Design Principles:
 * 1. Single-frame overlap: Only the last frame from the previous chunk is retained
 * 2. Verification state continuity: Verification states are preserved across chunks
 * 3. Simplified index management: Global indices are calculated using simple offsets
 * 4. Memory efficiency: Chunks are processed and released immediately after use
 *
 * Memory Usage Pattern:
 * - Producer thread: Holds 1 chunk worth of frames during extraction
 * - Shared queue: Holds reference to 1 chunk (capacity = 1)
 * - Consumer thread: Processes workingFrames = lastFrame + currentChunk
 * - ProcessingState: Retains only 1 frame (lastFrame) between chunks
 * - Peak memory: ~2 chunks worth of frames (significantly reduced from original)
 */

// Note: This implementation file primarily contains the data structure definitions
// from the header file. The actual chunk processing logic will be implemented
// in the SlideDetector class through the detectSlidesFromChunk method, and
// the producer-consumer coordination will be handled in the ProcessingThread class.

// The structures defined in chunkprocessor.h are designed to be:
// 1. Lightweight and efficient for frequent copying/moving
// 2. Self-contained with clear ownership semantics
// 3. Compatible with the existing Qt signal/slot system
// 4. Thread-safe when used with proper synchronization primitives

// Future enhancements could include:
// - Memory pool allocation for FrameChunk objects to reduce allocation overhead
// - Compression of lastFrame in ProcessingState for very large frames
// - Statistics collection for memory usage monitoring
// - Adaptive chunk sizing based on available memory