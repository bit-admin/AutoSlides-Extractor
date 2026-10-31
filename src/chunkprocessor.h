#ifndef CHUNKPROCESSOR_H
#define CHUNKPROCESSOR_H

#include <vector>
#include <memory>
#include <opencv2/opencv.hpp>
#include "memoryoptimizer.h"

/**
 * Processing state for chunk-based slide detection.
 *
 * Detection is a streaming state machine over the SSIM score sequence, so the
 * only things that must survive a chunk boundary are: the previous frame (to
 * score the boundary pair), whether a candidate slide is being verified, and
 * how many more stable scores it still needs. With that, chunked processing
 * gives exactly the same result as a single pass over the whole video.
 */
struct ProcessingState {
    // Global indices of all saved slides, in increasing order
    std::vector<int> savedSlideIndices;
    int lastStableIndex = -1;               // Global index of the last saved slide

    // Last frame of the previous chunk, already downsampled + grayscale
    // (only used to score the pair that straddles the chunk boundary).
    cv::Mat lastFramePrepared;
    int lastFrameGlobalIndex = -1;          // -1 until the first chunk has been processed
    double lastFrameTimestamp = 0.0;        // Media PTS seconds of that frame

    // Candidate slide under verification (may span chunk boundaries)
    int verifyRemaining = 0;                // Stable scores still needed; 0 = not verifying
    int candidateFrameCount = 0;            // Identical frames seen so far for the candidate
    double pendingChangeAt = 0.0;           // PTS of the candidate's first frame

    // Global index of newFrames[0] for the chunk being processed
    int globalFrameOffset = 0;

    void reset() { *this = ProcessingState(); }

    bool isFirstChunk() const { return lastFrameGlobalIndex < 0; }
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
    // Parallel media PTS seconds for each frame in this chunk (same length as frames/frameBuffers).
    std::vector<double> timestamps;

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
     * Constructor with Mat vector + media timestamps
     */
    FrameChunk(const std::vector<cv::Mat>& frameData, const std::vector<double>& ptsSeconds,
               int offset, bool isLast) :
        frames(frameData), startOffset(offset), isLastChunk(isLast),
        useOptimizedStorage(false), timestamps(ptsSeconds) {}

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
     * Constructor with FrameBuffer vector + media timestamps
     */
    FrameChunk(std::vector<FrameBuffer>&& frameBufferData, std::vector<double>&& ptsSeconds,
               int offset, bool isLast) :
        frameBuffers(std::move(frameBufferData)), startOffset(offset),
        isLastChunk(isLast), useOptimizedStorage(true), timestamps(std::move(ptsSeconds)) {}

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
        useOptimizedStorage(other.useOptimizedStorage),
        timestamps(std::move(other.timestamps)) {}

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
            timestamps = std::move(other.timestamps);
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