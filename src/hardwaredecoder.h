#ifndef HARDWAREDECODER_H
#define HARDWAREDECODER_H

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <atomic>
#include <opencv2/opencv.hpp>
#include "memoryoptimizer.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>
#ifdef __APPLE__
#include <VideoToolbox/VideoToolbox.h>
#endif
}

/**
 * Hardware-accelerated video decoder using FFmpeg C API
 * Supports cross-platform hardware acceleration:
 * - macOS: VideoToolbox
 * - Windows: D3D11VA, DXVA2
 * - Linux: VAAPI, VDPAU
 * - Automatic fallback to software decoding if hardware acceleration is unavailable
 */
class HardwareDecoder
{
public:
    /**
     * Video information structure
     */
    struct VideoInfo {
        double duration = 0.0;          // Duration in seconds
        double frameRate = 0.0;         // Frame rate
        int width = 0;                  // Video width
        int height = 0;                 // Video height
        double avgIFrameInterval = 0.0; // Average I-frame interval in seconds
        bool isScreenRecording = false; // Whether this appears to be a screen recording
        std::string codecName;          // Codec name
    };

    /**
     * Frame sampling strategy based on I-frame analysis
     */
    enum class SamplingStrategy {
        UseAllIFrames,      // Use all I-frames (interval ~2s)
        SkipEveryOtherI,    // Skip every other I-frame (interval ~1-1.5s)
        UseAllIFramesWarn   // Use all I-frames but warn user (interval >=4s)
    };

    /**
     * Progress callback function type
     * Parameters: current_time_seconds, total_duration_seconds, progress_percentage
     */
    using ProgressCallback = std::function<void(double, double, double)>;

    /**
     * Frame callback function type
     * Parameters: frame_mat, timestamp_seconds, frame_number
     */
    using FrameCallback = std::function<void(const cv::Mat&, double, int)>;

    /**
     * Optimized frame callback function type using FrameBuffer
     * Parameters: frame_buffer, timestamp_seconds, frame_number
     */
    using FrameBufferCallback = std::function<void(FrameBuffer&&, double, int)>;

    /**
     * Chunk ready callback function type for chunk-based processing
     * Parameters: frames_vector, start_offset, is_last_chunk
     */
    using ChunkReadyCallback = std::function<void(const std::vector<cv::Mat>&, int startOffset, bool isLastChunk)>;

    /**
     * Optimized chunk ready callback function type using FrameBuffer
     * Parameters: frame_buffers_vector, start_offset, is_last_chunk
     */
    using OptimizedChunkReadyCallback = std::function<void(std::vector<FrameBuffer>&&, int startOffset, bool isLastChunk)>;

    HardwareDecoder();
    ~HardwareDecoder();

    /**
     * Open video file and analyze its properties
     * @param videoPath Path to video file
     * @return true if successful
     */
    bool openVideo(const std::string& videoPath);

    /**
     * Get video information
     * @return VideoInfo structure with video properties
     */
    const VideoInfo& getVideoInfo() const { return m_videoInfo; }

    /**
     * Analyze I-frame distribution and determine optimal sampling strategy
     * @return Recommended sampling strategy
     */
    SamplingStrategy analyzeIFrameDistribution();

    /**
     * Extract frames using intelligent I-frame sampling
     * @param frameCallback Callback function called for each extracted frame
     * @param progressCallback Optional progress callback
     * @param targetInterval Target interval in seconds (default: 2.0, used as reference)
     * @return Number of frames extracted, -1 on error
     */
    int extractFramesIntelligent(const FrameCallback& frameCallback,
                               const ProgressCallback& progressCallback = nullptr,
                               double targetInterval = 2.0);

    /**
     * Extract frames at specified intervals (legacy mode)
     * @param frameCallback Callback function called for each extracted frame
     * @param progressCallback Optional progress callback
     * @param intervalSeconds Interval between frames
     * @return Number of frames extracted, -1 on error
     */
    int extractFramesAtInterval(const FrameCallback& frameCallback,
                              const ProgressCallback& progressCallback = nullptr,
                              double intervalSeconds = 2.0);

    /**
     * Extract frames in chunks for memory-optimized processing
     * @param chunkCallback Callback function called when each chunk is ready
     * @param progressCallback Optional progress callback
     * @param chunkSize Number of frames per chunk
     * @param targetInterval Target interval in seconds (default: 2.0, used as reference)
     * @return Number of frames extracted, -1 on error
     */
    int extractFramesInChunks(const ChunkReadyCallback& chunkCallback,
                            const ProgressCallback& progressCallback = nullptr,
                            int chunkSize = 100,
                            double targetInterval = 2.0);

    /**
     * Extract frames using zero-copy FrameBuffer (optimized)
     * @param frameCallback Callback function called for each extracted frame
     * @param progressCallback Optional progress callback
     * @param targetInterval Target interval in seconds (default: 2.0, used as reference)
     * @return Number of frames extracted, -1 on error
     */
    int extractFramesOptimized(const FrameBufferCallback& frameCallback,
                             const ProgressCallback& progressCallback = nullptr,
                             double targetInterval = 2.0);

    /**
     * Extract frames in chunks using zero-copy FrameBuffer (most optimized)
     * @param chunkCallback Callback function called when each chunk is ready
     * @param progressCallback Optional progress callback
     * @param chunkSize Number of frames per chunk
     * @param targetInterval Target interval in seconds (default: 2.0, used as reference)
     * @return Number of frames extracted, -1 on error
     */
    int extractFramesInChunksOptimized(const OptimizedChunkReadyCallback& chunkCallback,
                                     const ProgressCallback& progressCallback = nullptr,
                                     int chunkSize = 100,
                                     double targetInterval = 2.0);

    /**
     * Close video and cleanup resources
     */
    void close();

    /**
     * Request cancellation of current operation
     * This will interrupt FFmpeg operations like av_read_frame
     */
    void requestCancellation();

    /**
     * Reset cancellation flag
     */
    void resetCancellation();

    /**
     * Check if hardware acceleration is available
     * @return true if hardware acceleration is supported and available
     */
    static bool isHardwareAccelerationAvailable();

    /**
     * Get name of the hardware acceleration method being used
     * @return Hardware acceleration method name, or "Software" if not using hardware
     */
    std::string getHardwareAccelerationMethod() const;

    /**
     * Get error message from last operation
     * @return Error message string
     */
    const std::string& getLastError() const { return m_lastError; }

private:
    /**
     * Initialize FFmpeg libraries (called once)
     */
    static void initializeFFmpeg();

    /**
     * Detect current platform and get preferred hardware device types
     * @return Vector of hardware device types in order of preference
     */
    static std::vector<AVHWDeviceType> getPreferredHardwareDeviceTypes();

    /**
     * Setup hardware decoder context with auto-detection
     * Tries multiple hardware acceleration methods in order of preference
     * @return true if successful
     */
    bool setupHardwareDecoder();

    /**
     * Try to setup hardware decoder with specific device type
     * @param deviceType Hardware device type to try
     * @return true if successful
     */
    bool tryHardwareDeviceType(AVHWDeviceType deviceType);

    /**
     * Setup software decoder context
     * @return true if successful
     */
    bool setupSoftwareDecoder();

    /**
     * Convert AVFrame to OpenCV Mat with zero-copy when possible
     * @param frame AVFrame to convert
     * @param mat Output OpenCV Mat
     * @return true if successful
     */
    bool convertFrameToMat(AVFrame* frame, cv::Mat& mat);

    /**
     * Convert AVFrame to FrameBuffer with zero-copy optimization
     * @param frame AVFrame to convert
     * @param frameBuffer Output FrameBuffer
     * @return true if successful
     */
    bool convertFrameToFrameBuffer(AVFrame* frame, FrameBuffer& frameBuffer);

    /**
     * Detect if video is likely a screen recording
     * @return true if appears to be screen recording
     */
    bool detectScreenRecording();

    /**
     * Analyze I-frame intervals in the video
     * @param maxFramesToAnalyze Maximum number of frames to analyze (0 = all)
     * @return Average I-frame interval in seconds
     */
    double analyzeIFrameIntervals(int maxFramesToAnalyze = 100);

    /**
     * Seek to specific timestamp
     * @param timestamp Timestamp in seconds
     * @return true if successful
     */
    bool seekToTimestamp(double timestamp);

    /**
     * Decode next frame
     * @param frame Output frame
     * @return true if frame decoded successfully, false if EOF or error
     */
    bool decodeNextFrame(AVFrame* frame);

    // FFmpeg context objects
    AVFormatContext* m_formatContext;
    AVCodecContext* m_codecContext;
    const AVCodec* m_codec;
    SwsContext* m_swsContext;
    AVFrame* m_frame;
    AVFrame* m_frameRGB;
    AVPacket* m_packet;

    // Hardware acceleration
    bool m_useHardwareAcceleration;
    AVHWDeviceType m_hwDeviceType;
    AVBufferRef* m_hwDeviceContext;
    AVFrame* m_hwFrame;

    // Video stream info
    int m_videoStreamIndex;
    VideoInfo m_videoInfo;
    SamplingStrategy m_samplingStrategy;

    // Error handling
    std::string m_lastError;

    // Memory management
    uint8_t* m_buffer;
    size_t m_bufferSize;

    // Cancellation support
    std::atomic<bool> m_shouldCancel;

    // Static initialization flag
    static bool s_ffmpegInitialized;

    // FFmpeg interrupt callback
    static int interruptCallback(void* ctx);
};

#endif // HARDWAREDECODER_H