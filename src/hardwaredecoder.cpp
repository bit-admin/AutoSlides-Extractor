#include "hardwaredecoder.h"
#include "memoryoptimizer.h"
#include <iostream>
#include <cmath>
#include <algorithm>

// Static member initialization
bool HardwareDecoder::s_ffmpegInitialized = false;

HardwareDecoder::HardwareDecoder()
    : m_formatContext(nullptr)
    , m_codecContext(nullptr)
    , m_codec(nullptr)
    , m_swsContext(nullptr)
    , m_frame(nullptr)
    , m_frameRGB(nullptr)
    , m_packet(nullptr)
    , m_useHardwareAcceleration(false)
    , m_hwDeviceType(AV_HWDEVICE_TYPE_NONE)
    , m_hwDeviceContext(nullptr)
    , m_hwFrame(nullptr)
    , m_videoStreamIndex(-1)
    , m_samplingStrategy(SamplingStrategy::UseAllIFrames)
    , m_buffer(nullptr)
    , m_bufferSize(0)
{
    initializeFFmpeg();
}

HardwareDecoder::~HardwareDecoder()
{
    close();
}

void HardwareDecoder::initializeFFmpeg()
{
    if (!s_ffmpegInitialized) {
        // Initialize FFmpeg (not needed in newer versions, but safe to call)
        s_ffmpegInitialized = true;
    }
}

bool HardwareDecoder::openVideo(const std::string& videoPath)
{
    close(); // Clean up any previous state

    // Open input file
    if (avformat_open_input(&m_formatContext, videoPath.c_str(), nullptr, nullptr) < 0) {
        m_lastError = "Could not open video file: " + videoPath;
        return false;
    }

    // Retrieve stream information
    if (avformat_find_stream_info(m_formatContext, nullptr) < 0) {
        m_lastError = "Could not find stream information";
        return false;
    }

    // Find video stream
    m_videoStreamIndex = -1;
    for (unsigned int i = 0; i < m_formatContext->nb_streams; i++) {
        if (m_formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_videoStreamIndex = i;
            break;
        }
    }

    if (m_videoStreamIndex == -1) {
        m_lastError = "Could not find video stream";
        return false;
    }

    // Get codec parameters
    AVCodecParameters* codecParams = m_formatContext->streams[m_videoStreamIndex]->codecpar;

    // Find decoder
    m_codec = avcodec_find_decoder(codecParams->codec_id);
    if (!m_codec) {
        m_lastError = "Unsupported codec";
        return false;
    }

    // Allocate codec context
    m_codecContext = avcodec_alloc_context3(m_codec);
    if (!m_codecContext) {
        m_lastError = "Could not allocate codec context";
        return false;
    }

    // Copy codec parameters to context
    if (avcodec_parameters_to_context(m_codecContext, codecParams) < 0) {
        m_lastError = "Could not copy codec parameters";
        return false;
    }

    // Fill video info
    m_videoInfo.width = codecParams->width;
    m_videoInfo.height = codecParams->height;
    m_videoInfo.codecName = m_codec->name;

    // Calculate duration
    if (m_formatContext->duration != AV_NOPTS_VALUE) {
        m_videoInfo.duration = (double)m_formatContext->duration / AV_TIME_BASE;
    } else {
        m_videoInfo.duration = 0.0;
    }

    // Calculate frame rate
    AVRational frameRate = m_formatContext->streams[m_videoStreamIndex]->r_frame_rate;
    if (frameRate.den != 0) {
        m_videoInfo.frameRate = (double)frameRate.num / frameRate.den;
    } else {
        m_videoInfo.frameRate = 25.0; // Default fallback
    }

    // Try hardware acceleration first, fallback to software
    if (!setupHardwareDecoder()) {
        if (!setupSoftwareDecoder()) {
            m_lastError = "Could not setup decoder";
            return false;
        }
    }

    // Allocate frames and packet
    m_frame = av_frame_alloc();
    m_frameRGB = av_frame_alloc();
    m_packet = av_packet_alloc();

    if (!m_frame || !m_frameRGB || !m_packet) {
        m_lastError = "Could not allocate frames or packet";
        return false;
    }

    // Analyze video properties
    m_videoInfo.avgIFrameInterval = analyzeIFrameIntervals();
    m_videoInfo.isScreenRecording = detectScreenRecording();

    return true;
}

std::vector<AVHWDeviceType> HardwareDecoder::getPreferredHardwareDeviceTypes()
{
    std::vector<AVHWDeviceType> deviceTypes;

#ifdef __APPLE__
    // macOS: Prefer VideoToolbox
    deviceTypes.push_back(AV_HWDEVICE_TYPE_VIDEOTOOLBOX);
#elif defined(_WIN32) || defined(_WIN64)
    // Windows: Prefer D3D11VA, fallback to DXVA2
    deviceTypes.push_back(AV_HWDEVICE_TYPE_D3D11VA);
    deviceTypes.push_back(AV_HWDEVICE_TYPE_DXVA2);
#elif defined(__linux__)
    // Linux: Prefer VAAPI, fallback to VDPAU
    deviceTypes.push_back(AV_HWDEVICE_TYPE_VAAPI);
    deviceTypes.push_back(AV_HWDEVICE_TYPE_VDPAU);
#endif

    // CUDA is available on all platforms (if NVIDIA GPU present)
    deviceTypes.push_back(AV_HWDEVICE_TYPE_CUDA);

    return deviceTypes;
}

bool HardwareDecoder::setupHardwareDecoder()
{
    // Get preferred hardware device types for current platform
    std::vector<AVHWDeviceType> deviceTypes = getPreferredHardwareDeviceTypes();

    // Try each device type in order of preference
    for (AVHWDeviceType deviceType : deviceTypes) {
        if (tryHardwareDeviceType(deviceType)) {
            m_hwDeviceType = deviceType;
            m_useHardwareAcceleration = true;
            return true;
        }
    }

    return false;
}

bool HardwareDecoder::tryHardwareDeviceType(AVHWDeviceType deviceType)
{
    // Check if codec supports this hardware device type
    for (int i = 0;; i++) {
        const AVCodecHWConfig* config = avcodec_get_hw_config(m_codec, i);
        if (!config) {
            // No more hardware configs available for this codec
            return false;
        }

        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
            config->device_type == deviceType) {
            // This device type is supported by the codec
            break;
        }
    }

    // Create hardware device context
    if (av_hwdevice_ctx_create(&m_hwDeviceContext, deviceType, nullptr, nullptr, 0) < 0) {
        return false;
    }

    // Set hardware device context
    m_codecContext->hw_device_ctx = av_buffer_ref(m_hwDeviceContext);

    // Open codec
    if (avcodec_open2(m_codecContext, m_codec, nullptr) < 0) {
        av_buffer_unref(&m_hwDeviceContext);
        m_hwDeviceContext = nullptr;
        return false;
    }

    // Allocate hardware frame
    m_hwFrame = av_frame_alloc();
    if (!m_hwFrame) {
        return false;
    }

    return true;
}

bool HardwareDecoder::setupSoftwareDecoder()
{
    // Open codec with software decoding
    if (avcodec_open2(m_codecContext, m_codec, nullptr) < 0) {
        return false;
    }

    m_useHardwareAcceleration = false;
    return true;
}

bool HardwareDecoder::isHardwareAccelerationAvailable()
{
    // Try to detect any available hardware acceleration
    std::vector<AVHWDeviceType> deviceTypes = getPreferredHardwareDeviceTypes();

    // If we have any preferred device types for this platform, assume hardware acceleration is available
    // Actual availability will be checked when trying to create the device context
    return !deviceTypes.empty();
}

std::string HardwareDecoder::getHardwareAccelerationMethod() const
{
    if (!m_useHardwareAcceleration) {
        return "Software";
    }

    const char* typeName = av_hwdevice_get_type_name(m_hwDeviceType);
    if (typeName) {
        return std::string(typeName);
    }

    return "Unknown";
}

double HardwareDecoder::analyzeIFrameIntervals(int maxFramesToAnalyze)
{
    if (!m_formatContext || m_videoStreamIndex < 0) {
        return 2.0; // Default fallback
    }

    std::vector<double> iFrameTimestamps;
    AVPacket* packet = av_packet_alloc();
    int framesAnalyzed = 0;

    // Save current position
    int64_t currentPos = avio_tell(m_formatContext->pb);

    // Seek to beginning
    av_seek_frame(m_formatContext, m_videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);

    while (av_read_frame(m_formatContext, packet) >= 0 &&
           (maxFramesToAnalyze == 0 || framesAnalyzed < maxFramesToAnalyze)) {

        if (packet->stream_index == m_videoStreamIndex) {
            // Check if this is an I-frame (keyframe)
            if (packet->flags & AV_PKT_FLAG_KEY) {
                double timestamp = (double)packet->pts * av_q2d(m_formatContext->streams[m_videoStreamIndex]->time_base);
                iFrameTimestamps.push_back(timestamp);
            }
            framesAnalyzed++;
        }
        av_packet_unref(packet);
    }

    av_packet_free(&packet);

    // Restore position
    avio_seek(m_formatContext->pb, currentPos, SEEK_SET);

    // Calculate average interval
    if (iFrameTimestamps.size() < 2) {
        return 2.0; // Default fallback
    }

    double totalInterval = 0.0;
    for (size_t i = 1; i < iFrameTimestamps.size(); i++) {
        totalInterval += iFrameTimestamps[i] - iFrameTimestamps[i-1];
    }

    return totalInterval / (iFrameTimestamps.size() - 1);
}

bool HardwareDecoder::detectScreenRecording()
{
    // Heuristics for screen recording detection:
    // 1. Common screen recording resolutions
    // 2. Specific codecs often used for screen recording
    // 3. Frame rate patterns

    // Check for common screen recording resolutions
    bool commonScreenRes = (
        (m_videoInfo.width == 1920 && m_videoInfo.height == 1080) ||
        (m_videoInfo.width == 2560 && m_videoInfo.height == 1440) ||
        (m_videoInfo.width == 3840 && m_videoInfo.height == 2160) ||
        (m_videoInfo.width == 1280 && m_videoInfo.height == 720) ||
        (m_videoInfo.width == 1366 && m_videoInfo.height == 768) ||
        (m_videoInfo.width == 1440 && m_videoInfo.height == 900)
    );

    // Check for screen recording codecs
    bool screenRecordingCodec = (
        m_videoInfo.codecName == "h264" ||
        m_videoInfo.codecName == "hevc" ||
        m_videoInfo.codecName == "prores"
    );

    // Check frame rate (screen recordings often use 30fps or 60fps)
    bool typicalFrameRate = (
        std::abs(m_videoInfo.frameRate - 30.0) < 1.0 ||
        std::abs(m_videoInfo.frameRate - 60.0) < 1.0 ||
        std::abs(m_videoInfo.frameRate - 25.0) < 1.0
    );

    // Check I-frame interval (screen recordings often have regular I-frame intervals)
    bool regularIFrames = (m_videoInfo.avgIFrameInterval > 0.5 && m_videoInfo.avgIFrameInterval < 10.0);

    // Combine heuristics
    int score = 0;
    if (commonScreenRes) score++;
    if (screenRecordingCodec) score++;
    if (typicalFrameRate) score++;
    if (regularIFrames) score++;

    return score >= 2; // Require at least 2 indicators
}

HardwareDecoder::SamplingStrategy HardwareDecoder::analyzeIFrameDistribution()
{
    double interval = m_videoInfo.avgIFrameInterval;

    if (interval >= 4.0) {
        return SamplingStrategy::UseAllIFramesWarn;
    } else if (interval >= 1.6 && interval <= 1.9) {
        return SamplingStrategy::UseAllIFrames;
    } else if (interval >= 1.0 && interval <= 1.5) {
        return SamplingStrategy::SkipEveryOtherI;
    } else {
        return SamplingStrategy::UseAllIFrames;
    }
}

int HardwareDecoder::extractFramesIntelligent(const FrameCallback& frameCallback,
                                            const ProgressCallback& progressCallback,
                                            double targetInterval)
{
    if (!m_formatContext || !m_codecContext || !frameCallback) {
        m_lastError = "Video not opened or invalid callback";
        return -1;
    }

    // Determine sampling strategy
    m_samplingStrategy = analyzeIFrameDistribution();

    // Seek to beginning
    av_seek_frame(m_formatContext, m_videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(m_codecContext);

    int frameCount = 0;
    int iFrameCount = 0;
    bool skipNext = false;

    while (av_read_frame(m_formatContext, m_packet) >= 0) {
        if (m_packet->stream_index == m_videoStreamIndex) {
            // Check if this is an I-frame
            bool isIFrame = (m_packet->flags & AV_PKT_FLAG_KEY);

            if (isIFrame) {
                // Apply sampling strategy
                bool shouldDecode = true;

                switch (m_samplingStrategy) {
                    case SamplingStrategy::SkipEveryOtherI:
                        shouldDecode = !skipNext;
                        skipNext = !skipNext;
                        break;
                    case SamplingStrategy::UseAllIFrames:
                    case SamplingStrategy::UseAllIFramesWarn:
                        shouldDecode = true;
                        break;
                }

                if (shouldDecode) {
                    // Decode the I-frame
                    if (avcodec_send_packet(m_codecContext, m_packet) >= 0) {
                        AVFrame* targetFrame = m_useHardwareAcceleration ? m_hwFrame : m_frame;

                        if (avcodec_receive_frame(m_codecContext, targetFrame) >= 0) {
                            cv::Mat mat;

                            if (convertFrameToMat(targetFrame, mat)) {
                                double timestamp = (double)m_packet->pts *
                                    av_q2d(m_formatContext->streams[m_videoStreamIndex]->time_base);

                                frameCallback(mat, timestamp, frameCount);
                                frameCount++;

                                // Progress callback
                                if (progressCallback && m_videoInfo.duration > 0) {
                                    double progress = (timestamp / m_videoInfo.duration) * 100.0;
                                    progressCallback(timestamp, m_videoInfo.duration, progress);
                                }
                            }
                        }
                    }
                }
                iFrameCount++;
            }
        }
        av_packet_unref(m_packet);
    }

    return frameCount;
}

int HardwareDecoder::extractFramesAtInterval(const FrameCallback& frameCallback,
                                           const ProgressCallback& progressCallback,
                                           double intervalSeconds)
{
    if (!m_formatContext || !m_codecContext || !frameCallback) {
        m_lastError = "Video not opened or invalid callback";
        return -1;
    }

    // Seek to beginning
    av_seek_frame(m_formatContext, m_videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(m_codecContext);

    int frameCount = 0;
    double nextTargetTime = 0.0;

    while (av_read_frame(m_formatContext, m_packet) >= 0) {
        if (m_packet->stream_index == m_videoStreamIndex) {
            double timestamp = (double)m_packet->pts *
                av_q2d(m_formatContext->streams[m_videoStreamIndex]->time_base);

            if (timestamp >= nextTargetTime) {
                // Decode this frame
                if (avcodec_send_packet(m_codecContext, m_packet) >= 0) {
                    AVFrame* targetFrame = m_useHardwareAcceleration ? m_hwFrame : m_frame;

                    if (avcodec_receive_frame(m_codecContext, targetFrame) >= 0) {
                        cv::Mat mat;

                        if (convertFrameToMat(targetFrame, mat)) {
                            frameCallback(mat, timestamp, frameCount);
                            frameCount++;
                            nextTargetTime += intervalSeconds;

                            // Progress callback
                            if (progressCallback && m_videoInfo.duration > 0) {
                                double progress = (timestamp / m_videoInfo.duration) * 100.0;
                                progressCallback(timestamp, m_videoInfo.duration, progress);
                            }
                        }
                    }
                }
            }
        }
        av_packet_unref(m_packet);
    }

    return frameCount;
}

int HardwareDecoder::extractFramesInChunks(const ChunkReadyCallback& chunkCallback,
                                         const ProgressCallback& progressCallback,
                                         int chunkSize,
                                         double targetInterval)
{
    if (!m_formatContext || !m_codecContext || !chunkCallback) {
        m_lastError = "Video not opened or invalid callback";
        return -1;
    }

    if (chunkSize <= 0) {
        m_lastError = "Invalid chunk size";
        return -1;
    }

    // Determine sampling strategy
    m_samplingStrategy = analyzeIFrameDistribution();

    // Seek to beginning
    av_seek_frame(m_formatContext, m_videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(m_codecContext);

    int totalFrameCount = 0;
    int iFrameCount = 0;
    bool skipNext = false;
    std::vector<cv::Mat> currentChunk;
    int chunkStartOffset = 0;

    while (av_read_frame(m_formatContext, m_packet) >= 0) {
        if (m_packet->stream_index == m_videoStreamIndex) {
            // Check if this is an I-frame
            bool isIFrame = (m_packet->flags & AV_PKT_FLAG_KEY);

            if (isIFrame) {
                // Apply sampling strategy
                bool shouldDecode = true;

                switch (m_samplingStrategy) {
                    case SamplingStrategy::SkipEveryOtherI:
                        shouldDecode = !skipNext;
                        skipNext = !skipNext;
                        break;
                    case SamplingStrategy::UseAllIFrames:
                    case SamplingStrategy::UseAllIFramesWarn:
                        shouldDecode = true;
                        break;
                }

                if (shouldDecode) {
                    // Decode the I-frame
                    if (avcodec_send_packet(m_codecContext, m_packet) >= 0) {
                        AVFrame* targetFrame = m_useHardwareAcceleration ? m_hwFrame : m_frame;

                        if (avcodec_receive_frame(m_codecContext, targetFrame) >= 0) {
                            cv::Mat mat;

                            if (convertFrameToMat(targetFrame, mat)) {
                                // Add frame to current chunk (mat is already cloned in convertFrameToMat)
                                currentChunk.push_back(mat);

                                // Progress callback
                                if (progressCallback && m_videoInfo.duration > 0) {
                                    double timestamp = (double)m_packet->pts *
                                        av_q2d(m_formatContext->streams[m_videoStreamIndex]->time_base);
                                    double progress = (timestamp / m_videoInfo.duration) * 100.0;
                                    progressCallback(timestamp, m_videoInfo.duration, progress);
                                }

                                totalFrameCount++;

                                // Check if chunk is full
                                if (currentChunk.size() >= static_cast<size_t>(chunkSize)) {
                                    // Call chunk callback with current chunk
                                    chunkCallback(currentChunk, chunkStartOffset, false);

                                    // Prepare for next chunk
                                    chunkStartOffset = totalFrameCount;
                                    currentChunk.clear();
                                }
                            }
                        }
                    }
                }
                iFrameCount++;
            }
        }
        av_packet_unref(m_packet);
    }

    // Handle remaining frames in the last chunk
    if (!currentChunk.empty()) {
        chunkCallback(currentChunk, chunkStartOffset, true); // Mark as last chunk
    }

    return totalFrameCount;
}

bool HardwareDecoder::convertFrameToMat(AVFrame* frame, cv::Mat& mat)
{
    if (!frame) {
        return false;
    }

    AVFrame* sourceFrame = frame;

    // If using hardware acceleration, transfer frame to system memory
    if (m_useHardwareAcceleration && frame->format != AV_PIX_FMT_YUV420P) {
        if (av_hwframe_transfer_data(m_frame, frame, 0) < 0) {
            return false;
        }
        sourceFrame = m_frame;
    }

    // Setup conversion context if needed
    if (!m_swsContext) {
        m_swsContext = sws_getContext(
            sourceFrame->width, sourceFrame->height, (AVPixelFormat)sourceFrame->format,
            sourceFrame->width, sourceFrame->height, AV_PIX_FMT_BGR24,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );

        if (!m_swsContext) {
            return false;
        }

        // Allocate buffer for RGB frame
        m_bufferSize = av_image_get_buffer_size(AV_PIX_FMT_BGR24, sourceFrame->width, sourceFrame->height, 1);
        m_buffer = (uint8_t*)av_malloc(m_bufferSize);

        if (!m_buffer) {
            return false;
        }

        av_image_fill_arrays(m_frameRGB->data, m_frameRGB->linesize, m_buffer,
                           AV_PIX_FMT_BGR24, sourceFrame->width, sourceFrame->height, 1);
    }

    // Convert frame to BGR24
    sws_scale(m_swsContext, sourceFrame->data, sourceFrame->linesize, 0, sourceFrame->height,
              m_frameRGB->data, m_frameRGB->linesize);

    // Create OpenCV Mat with zero-copy (data is not copied, just wrapped)
    mat = cv::Mat(sourceFrame->height, sourceFrame->width, CV_8UC3, m_frameRGB->data[0], m_frameRGB->linesize[0]);

    // Clone the mat to ensure data persistence after frame is reused
    mat = mat.clone();

    return true;
}

bool HardwareDecoder::convertFrameToFrameBuffer(AVFrame* frame, FrameBuffer& frameBuffer)
{
    if (!frame) {
        return false;
    }

    AVFrame* sourceFrame = frame;

    // If using hardware acceleration, transfer frame to system memory
    if (m_useHardwareAcceleration && frame->format != AV_PIX_FMT_YUV420P) {
        if (av_hwframe_transfer_data(m_frame, frame, 0) < 0) {
            return false;
        }
        sourceFrame = m_frame;
    }

    // Setup conversion context if needed
    if (!m_swsContext) {
        m_swsContext = sws_getContext(
            sourceFrame->width, sourceFrame->height, (AVPixelFormat)sourceFrame->format,
            sourceFrame->width, sourceFrame->height, AV_PIX_FMT_BGR24,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );

        if (!m_swsContext) {
            return false;
        }

        // Allocate buffer for RGB frame
        m_bufferSize = av_image_get_buffer_size(AV_PIX_FMT_BGR24, sourceFrame->width, sourceFrame->height, 1);
        m_buffer = (uint8_t*)av_malloc(m_bufferSize);

        if (!m_buffer) {
            return false;
        }

        av_image_fill_arrays(m_frameRGB->data, m_frameRGB->linesize, m_buffer,
                           AV_PIX_FMT_BGR24, sourceFrame->width, sourceFrame->height, 1);
    }

    // Convert frame to BGR24
    sws_scale(m_swsContext, sourceFrame->data, sourceFrame->linesize, 0, sourceFrame->height,
              m_frameRGB->data, m_frameRGB->linesize);

    // Calculate the actual data size needed
    size_t dataSize = sourceFrame->height * m_frameRGB->linesize[0];

    // Allocate aligned memory for the FrameBuffer
    uint8_t* alignedData = static_cast<uint8_t*>(AlignedAllocator::allocate(dataSize, 32));
    if (!alignedData) {
        return false;
    }

    // Copy data to aligned buffer (this is the only copy, but it's to aligned memory)
    std::memcpy(alignedData, m_frameRGB->data[0], dataSize);

    // Create FrameBuffer that takes ownership of the aligned data
    frameBuffer = FrameBuffer(alignedData, sourceFrame->height, sourceFrame->width, CV_8UC3, dataSize);

    return frameBuffer.isValid();
}

int HardwareDecoder::extractFramesOptimized(const FrameBufferCallback& frameCallback,
                                           const ProgressCallback& progressCallback,
                                           double targetInterval)
{
    if (!m_formatContext || !m_codecContext || !frameCallback) {
        m_lastError = "Video not opened or invalid callback";
        return -1;
    }

    // Determine sampling strategy
    m_samplingStrategy = analyzeIFrameDistribution();

    // Seek to beginning
    av_seek_frame(m_formatContext, m_videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(m_codecContext);

    int totalFrameCount = 0;
    int iFrameCount = 0;
    bool skipNext = false;

    while (av_read_frame(m_formatContext, m_packet) >= 0) {
        if (m_packet->stream_index == m_videoStreamIndex) {
            // Check if this is an I-frame
            bool isIFrame = (m_packet->flags & AV_PKT_FLAG_KEY);

            if (isIFrame) {
                // Apply sampling strategy
                bool shouldDecode = true;

                switch (m_samplingStrategy) {
                    case SamplingStrategy::SkipEveryOtherI:
                        shouldDecode = !skipNext;
                        skipNext = !skipNext;
                        break;
                    case SamplingStrategy::UseAllIFrames:
                    case SamplingStrategy::UseAllIFramesWarn:
                        shouldDecode = true;
                        break;
                }

                if (shouldDecode) {
                    // Decode the I-frame
                    if (avcodec_send_packet(m_codecContext, m_packet) >= 0) {
                        AVFrame* targetFrame = m_useHardwareAcceleration ? m_hwFrame : m_frame;

                        if (avcodec_receive_frame(m_codecContext, targetFrame) >= 0) {
                            FrameBuffer frameBuffer;

                            if (convertFrameToFrameBuffer(targetFrame, frameBuffer)) {
                                // Calculate timestamp
                                double timestamp = (double)m_packet->pts *
                                    av_q2d(m_formatContext->streams[m_videoStreamIndex]->time_base);

                                // Call frame callback with zero-copy FrameBuffer
                                frameCallback(std::move(frameBuffer), timestamp, totalFrameCount);

                                // Progress callback
                                if (progressCallback && m_videoInfo.duration > 0) {
                                    double progress = (timestamp / m_videoInfo.duration) * 100.0;
                                    progressCallback(timestamp, m_videoInfo.duration, progress);
                                }

                                totalFrameCount++;
                            }
                        }
                    }
                }
                iFrameCount++;
            }
        }
        av_packet_unref(m_packet);
    }

    return totalFrameCount;
}

int HardwareDecoder::extractFramesInChunksOptimized(const OptimizedChunkReadyCallback& chunkCallback,
                                                   const ProgressCallback& progressCallback,
                                                   int chunkSize,
                                                   double targetInterval)
{
    if (!m_formatContext || !m_codecContext || !chunkCallback) {
        m_lastError = "Video not opened or invalid callback";
        return -1;
    }

    if (chunkSize <= 0) {
        m_lastError = "Invalid chunk size";
        return -1;
    }

    // Determine sampling strategy
    m_samplingStrategy = analyzeIFrameDistribution();

    // Seek to beginning
    av_seek_frame(m_formatContext, m_videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(m_codecContext);

    int totalFrameCount = 0;
    int iFrameCount = 0;
    bool skipNext = false;
    std::vector<FrameBuffer> currentChunk;
    currentChunk.reserve(chunkSize); // Pre-allocate to avoid reallocations
    int chunkStartOffset = 0;

    while (av_read_frame(m_formatContext, m_packet) >= 0) {
        if (m_packet->stream_index == m_videoStreamIndex) {
            // Check if this is an I-frame
            bool isIFrame = (m_packet->flags & AV_PKT_FLAG_KEY);

            if (isIFrame) {
                // Apply sampling strategy
                bool shouldDecode = true;

                switch (m_samplingStrategy) {
                    case SamplingStrategy::SkipEveryOtherI:
                        shouldDecode = !skipNext;
                        skipNext = !skipNext;
                        break;
                    case SamplingStrategy::UseAllIFrames:
                    case SamplingStrategy::UseAllIFramesWarn:
                        shouldDecode = true;
                        break;
                }

                if (shouldDecode) {
                    // Decode the I-frame
                    if (avcodec_send_packet(m_codecContext, m_packet) >= 0) {
                        AVFrame* targetFrame = m_useHardwareAcceleration ? m_hwFrame : m_frame;

                        if (avcodec_receive_frame(m_codecContext, targetFrame) >= 0) {
                            FrameBuffer frameBuffer;

                            if (convertFrameToFrameBuffer(targetFrame, frameBuffer)) {
                                // Add FrameBuffer to current chunk (move semantics)
                                currentChunk.emplace_back(std::move(frameBuffer));

                                // Progress callback
                                if (progressCallback && m_videoInfo.duration > 0) {
                                    double timestamp = (double)m_packet->pts *
                                        av_q2d(m_formatContext->streams[m_videoStreamIndex]->time_base);
                                    double progress = (timestamp / m_videoInfo.duration) * 100.0;
                                    progressCallback(timestamp, m_videoInfo.duration, progress);
                                }

                                totalFrameCount++;

                                // Check if chunk is full
                                if (currentChunk.size() >= static_cast<size_t>(chunkSize)) {
                                    // Call chunk callback with current chunk (move semantics)
                                    chunkCallback(std::move(currentChunk), chunkStartOffset, false);

                                    // Prepare for next chunk
                                    chunkStartOffset = totalFrameCount;
                                    currentChunk.clear();
                                    currentChunk.reserve(chunkSize); // Re-reserve capacity
                                }
                            }
                        }
                    }
                }
                iFrameCount++;
            }
        }
        av_packet_unref(m_packet);
    }

    // Handle remaining frames in the last chunk
    if (!currentChunk.empty()) {
        chunkCallback(std::move(currentChunk), chunkStartOffset, true); // Mark as last chunk
    }

    return totalFrameCount;
}

void HardwareDecoder::close()
{
    // Free frames
    if (m_frame) {
        av_frame_free(&m_frame);
    }
    if (m_frameRGB) {
        av_frame_free(&m_frameRGB);
    }
    if (m_hwFrame) {
        av_frame_free(&m_hwFrame);
    }

    // Free packet
    if (m_packet) {
        av_packet_free(&m_packet);
    }

    // Free conversion context
    if (m_swsContext) {
        sws_freeContext(m_swsContext);
        m_swsContext = nullptr;
    }

    // Free buffer
    if (m_buffer) {
        av_free(m_buffer);
        m_buffer = nullptr;
    }

    // Free codec context
    if (m_codecContext) {
        avcodec_free_context(&m_codecContext);
    }

    // Free format context
    if (m_formatContext) {
        avformat_close_input(&m_formatContext);
    }

    // Free hardware device context
    if (m_hwDeviceContext) {
        av_buffer_unref(&m_hwDeviceContext);
    }

    // Reset state
    m_videoStreamIndex = -1;
    m_useHardwareAcceleration = false;
    m_bufferSize = 0;
    m_lastError.clear();
}