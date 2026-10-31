#include "chunkprocessor.h"

/**
 * Chunk-based processing: design notes
 *
 * Slide detection (SlideDetector::detectSlidesFromChunk) is a streaming state
 * machine over SSIM scores. ProcessingState carries everything it needs across
 * a chunk boundary:
 * - the previous chunk's last frame, already downsampled to grayscale, so the
 *   pair that straddles the boundary is scored;
 * - the candidate under verification (frames still needed, its change time).
 * Chunked processing therefore gives exactly the same slides and timings as a
 * single pass, whatever the chunk size.
 *
 * Memory usage pattern (chunkSize full-resolution BGR frames per chunk):
 * - Producer thread: the chunk being filled by the decoder
 * - Shared queue: at most 1 finished chunk (capacity = 1)
 * - Consumer thread: the chunk being analysed
 * - Peak: ~3 chunks of frames (+ one small grayscale frame in ProcessingState)
 */
