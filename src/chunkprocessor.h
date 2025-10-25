#ifndef CHUNKPROCESSOR_H
#define CHUNKPROCESSOR_H

#include <vector>
#include <memory>
#include <opencv2/opencv.hpp>
#include "memoryoptimizer.h"

/**
 * Verification state for the chunk-based slide detection algorithm
 * Tracks the verification progress of frames across chunk boundaries
 */
enum class VerificationState {
    NONE = 0,           // Non-verification state - no ongoing verification
    VERIFICATION_1OF3,  // Verification state 1 of 3 - first verification frame
    VERIFICATION_2OF3,  // Verification state 2 of 3 - second verification frame
    VERIFICATION_3OF3   // Verification state 3 of 3 - third verification frame (stable)
};

/**
 * Processing state structure for chunk-based slide detection
 * Maintains state information across multiple chunks to enable continuous processing
 * while minimizing memory usage through single-frame overlap and zero-copy operations
 */
struct ProcessingState {
    // Global slide detection results
    std::vector<int> savedSlideIndices;     // Global indices of saved slides
    int lastStableIndex;                    // Global index of the last stable frame

    // Zero-copy single-frame overlap mechanism
    FrameBuffer lastFrameBuffer;            // Last frame from the previous chunk (zero-copy)
    int lastFrameGlobalIndex;               // Global index of the last frame

    // Verification state management for cross-chunk continuity
    VerificationState lastFrameVerificationState;  // Verification state of the last frame
    int verificationStartIndex;             // Global index where current verification started

    // Global frame tracking
    int globalFrameOffset;                  // Current global frame offset for index calculation

    // Memory optimization components
    std::shared_ptr<MatMemoryPool> memoryPool;  // Shared memory pool for Mat reuse

    /**
     * Default constructor initializes all fields to safe default values
     */
    ProcessingState() :
        lastStableIndex(-1),
        lastFrameGlobalIndex(-1),
        lastFrameVerificationState(VerificationState::NONE),
        verificationStartIndex(-1),
        globalFrameOffset(0),
        memoryPool(std::make_shared<MatMemoryPool>(100))  // Initialize with pool size of 100
    {}

    /**
     * Reset the processing state for a new video
     * Clears all accumulated data while preserving the structure
     */
    void reset() {
        savedSlideIndices.clear();
        lastStableIndex = -1;
        lastFrameBuffer.reset();
        lastFrameGlobalIndex = -1;
        lastFrameVerificationState = VerificationState::NONE;
        verificationStartIndex = -1;
        globalFrameOffset = 0;
        // Keep memory pool but clean up old buffers
        if (memoryPool) {
            memoryPool->cleanupUnusedBuffers(30); // Clean buffers older than 30 seconds
        }
    }

    /**
     * Check if this is the first chunk being processed
     * @return true if no previous frame exists (first chunk)
     */
    bool isFirstChunk() const {
        return !lastFrameBuffer.isValid();
    }

    /**
     * Get the last frame as a Mat view (zero-copy)
     * @return Mat view of the last frame, or empty Mat if none
     */
    cv::Mat getLastFrameView() const {
        return lastFrameBuffer.isValid() ? lastFrameBuffer.getConstMatView() : cv::Mat();
    }

    /**
     * Set the last frame from a FrameBuffer (move operation)
     * @param frameBuffer FrameBuffer to move
     */
    void setLastFrame(FrameBuffer&& frameBuffer) {
        lastFrameBuffer = std::move(frameBuffer);
    }

    /**
     * Set the last frame from a Mat (creates FrameBuffer copy)
     * @param frame Mat to copy into FrameBuffer
     */
    void setLastFrame(const cv::Mat& frame) {
        lastFrameBuffer = FrameBuffer::fromMat(frame);
    }

    /**
     * Update the global frame offset after processing a chunk
     * @param processedFrameCount Number of new frames processed in the current chunk
     */
    void updateGlobalOffset(int processedFrameCount) {
        globalFrameOffset += processedFrameCount;
    }
};

/**
 * Frame chunk structure for the producer-consumer processing model
 * Represents a batch of frames extracted from the video with metadata
 * Supports both traditional Mat storage and optimized memory-mapped storage
 */
struct FrameChunk {
    // Traditional Mat storage (for backward compatibility)
    std::vector<cv::Mat> frames;    // Frame data for this chunk

    // Optimized storage options
    std::unique_ptr<MappedFrameChunk> mappedChunk;  // Memory-mapped chunk for large datasets
    std::vector<FrameBuffer> frameBuffers;          // Zero-copy frame buffers

    // Metadata
    int startOffset;                // Starting offset of this chunk in the video (frame index)
    bool isLastChunk;               // Flag indicating if this is the final chunk
    bool useOptimizedStorage;       // Flag to indicate which storage method is active

    /**
     * Default constructor
     */
    FrameChunk() : startOffset(0), isLastChunk(false), useOptimizedStorage(false) {}

    /**
     * Constructor with traditional Mat vector
     * @param frameData Vector of frames for this chunk
     * @param offset Starting frame offset in the video
     * @param isLast Whether this is the last chunk
     */
    FrameChunk(const std::vector<cv::Mat>& frameData, int offset, bool isLast) :
        frames(frameData), startOffset(offset), isLastChunk(isLast), useOptimizedStorage(false) {}

    /**
     * Constructor with FrameBuffer vector (optimized)
     * @param frameBufferData Vector of FrameBuffers for this chunk
     * @param offset Starting frame offset in the video
     * @param isLast Whether this is the last chunk
     */
    FrameChunk(std::vector<FrameBuffer>&& frameBufferData, int offset, bool isLast) :
        frameBuffers(std::move(frameBufferData)), startOffset(offset),
        isLastChunk(isLast), useOptimizedStorage(true) {}

    /**
     * Constructor with memory-mapped chunk (most optimized)
     * @param mappedChunkPtr Unique pointer to MappedFrameChunk
     * @param offset Starting frame offset in the video
     * @param isLast Whether this is the last chunk
     */
    FrameChunk(std::unique_ptr<MappedFrameChunk> mappedChunkPtr, int offset, bool isLast) :
        mappedChunk(std::move(mappedChunkPtr)), startOffset(offset),
        isLastChunk(isLast), useOptimizedStorage(true) {}

    /**
     * Move constructor
     */
    FrameChunk(FrameChunk&& other) noexcept :
        frames(std::move(other.frames)),
        mappedChunk(std::move(other.mappedChunk)),
        frameBuffers(std::move(other.frameBuffers)),
        startOffset(other.startOffset),
        isLastChunk(other.isLastChunk),
        useOptimizedStorage(other.useOptimizedStorage) {}

    /**
     * Move assignment operator
     */
    FrameChunk& operator=(FrameChunk&& other) noexcept {
        if (this != &other) {
            frames = std::move(other.frames);
            mappedChunk = std::move(other.mappedChunk);
            frameBuffers = std::move(other.frameBuffers);
            startOffset = other.startOffset;
            isLastChunk = other.isLastChunk;
            useOptimizedStorage = other.useOptimizedStorage;
        }
        return *this;
    }

    /**
     * Deleted copy operations to prevent accidental copies
     */
    FrameChunk(const FrameChunk&) = delete;
    FrameChunk& operator=(const FrameChunk&) = delete;

    /**
     * Get the number of frames in this chunk
     * @return Frame count
     */
    size_t size() const {
        if (mappedChunk) {
            return mappedChunk->frameCount();
        } else if (useOptimizedStorage) {
            return frameBuffers.size();
        } else {
            return frames.size();
        }
    }

    /**
     * Check if the chunk is empty
     * @return true if no frames are present
     */
    bool empty() const {
        return size() == 0;
    }

    /**
     * Get the ending offset of this chunk (exclusive)
     * @return End offset (startOffset + frame count)
     */
    int endOffset() const {
        return startOffset + static_cast<int>(size());
    }

    /**
     * Get a frame by index (returns Mat view, zero-copy when possible)
     * @param index Frame index within this chunk
     * @return Mat view of the frame, or empty Mat if index invalid
     */
    cv::Mat getFrame(size_t index) const {
        if (mappedChunk) {
            return mappedChunk->getFrame(index);
        } else if (useOptimizedStorage) {
            if (index < frameBuffers.size() && frameBuffers[index].isValid()) {
                return frameBuffers[index].getConstMatView();
            }
        } else {
            if (index < frames.size()) {
                return frames[index];
            }
        }
        return cv::Mat();
    }

    /**
     * Get all frames as Mat views (zero-copy when possible)
     * @return Vector of Mat views
     */
    std::vector<cv::Mat> getAllFrames() const {
        std::vector<cv::Mat> result;
        size_t frameCount = size();
        result.reserve(frameCount);

        if (mappedChunk) {
            const auto& views = mappedChunk->getFrameViews();
            result.assign(views.begin(), views.end());
        } else if (useOptimizedStorage) {
            for (const auto& buffer : frameBuffers) {
                if (buffer.isValid()) {
                    result.push_back(buffer.getConstMatView());
                }
            }
        } else {
            result = frames;
        }

        return result;
    }

    /**
     * Add a frame to the chunk (chooses optimal storage method)
     * @param frame Frame to add
     * @return true if frame was added successfully
     */
    bool addFrame(const cv::Mat& frame) {
        if (mappedChunk) {
            return mappedChunk->addFrame(frame);
        } else if (useOptimizedStorage) {
            frameBuffers.emplace_back(FrameBuffer::fromMat(frame));
            return frameBuffers.back().isValid();
        } else {
            frames.push_back(frame);
            return true;
        }
    }

    /**
     * Add a FrameBuffer to the chunk (zero-copy when possible)
     * @param frameBuffer FrameBuffer to add
     * @return true if frame was added successfully
     */
    bool addFrameBuffer(FrameBuffer&& frameBuffer) {
        if (mappedChunk) {
            return mappedChunk->addFrame(frameBuffer);
        } else {
            // Switch to optimized storage if not already
            if (!useOptimizedStorage && !frames.empty()) {
                // Convert existing frames to FrameBuffers
                for (const auto& frame : frames) {
                    frameBuffers.emplace_back(FrameBuffer::fromMat(frame));
                }
                frames.clear();
                useOptimizedStorage = true;
            } else if (!useOptimizedStorage) {
                useOptimizedStorage = true;
            }

            frameBuffers.emplace_back(std::move(frameBuffer));
            return frameBuffers.back().isValid();
        }
    }

    /**
     * Get memory usage statistics
     * @return Estimated memory usage in bytes
     */
    size_t getMemoryUsage() const {
        if (mappedChunk) {
            return mappedChunk->getMemoryUsage();
        } else if (useOptimizedStorage) {
            size_t total = 0;
            for (const auto& buffer : frameBuffers) {
                total += buffer.size();
            }
            return total;
        } else {
            size_t total = 0;
            for (const auto& frame : frames) {
                total += frame.total() * frame.elemSize();
            }
            return total;
        }
    }
};

#endif // CHUNKPROCESSOR_H