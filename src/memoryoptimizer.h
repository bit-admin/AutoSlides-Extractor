#ifndef MEMORYOPTIMIZER_H
#define MEMORYOPTIMIZER_H

#include <memory>
#include <vector>
#include <mutex>
#include <queue>
#include <opencv2/opencv.hpp>
#include <cstdlib>

// Platform-specific includes
#ifdef _WIN32
    #include <windows.h>
    #include <malloc.h>
#else
    #include <sys/mman.h>
    #include <unistd.h>
#endif

/**
 * Custom aligned allocator for SIMD optimization
 * Provides 32-byte aligned memory allocation for optimal SIMD performance
 */
class AlignedAllocator {
public:
    /**
     * Allocate aligned memory for SIMD operations
     * @param size Size in bytes to allocate
     * @param alignment Alignment requirement (default: 32 bytes for AVX2/NEON)
     * @return Pointer to aligned memory or nullptr on failure
     */
    static void* allocate(size_t size, size_t alignment = 32);

    /**
     * Deallocate aligned memory
     * @param ptr Pointer to memory allocated with allocate()
     */
    static void deallocate(void* ptr);

    /**
     * Check if a pointer is properly aligned
     * @param ptr Pointer to check
     * @param alignment Required alignment
     * @return true if pointer is aligned
     */
    static bool isAligned(const void* ptr, size_t alignment = 32);

    /**
     * Get the system page size
     * @return System page size in bytes
     */
    static size_t getPageSize();

    /**
     * Allocate large pages if supported by the platform
     * @param size Size in bytes to allocate
     * @return Pointer to large page memory or nullptr on failure
     */
    static void* allocateLargePages(size_t size);

    /**
     * Deallocate large page memory
     * @param ptr Pointer to memory allocated with allocateLargePages()
     * @param size Size of the allocated memory
     */
    static void deallocateLargePages(void* ptr, size_t size);
};

/**
 * Zero-copy frame buffer that wraps raw data without copying
 * Provides efficient Mat views into pre-allocated memory
 */
class FrameBuffer {
private:
    std::unique_ptr<uint8_t[], void(*)(void*)> m_data;  // Aligned raw data with custom deleter
    cv::Mat m_mat;                                       // Mat wrapper (no data copy)
    size_t m_size;                                       // Total buffer size
    bool m_isValid;                                      // Buffer validity flag

public:
    /**
     * Default constructor - creates empty buffer
     */
    FrameBuffer();

    /**
     * Constructor with dimensions and type
     * @param rows Image height
     * @param cols Image width
     * @param type OpenCV matrix type (e.g., CV_8UC3)
     */
    FrameBuffer(int rows, int cols, int type);

    /**
     * Constructor from existing data (takes ownership)
     * @param data Raw image data (must be aligned)
     * @param rows Image height
     * @param cols Image width
     * @param type OpenCV matrix type
     * @param dataSize Size of the data buffer
     */
    FrameBuffer(uint8_t* data, int rows, int cols, int type, size_t dataSize);

    /**
     * Move constructor
     */
    FrameBuffer(FrameBuffer&& other) noexcept;

    /**
     * Move assignment operator
     */
    FrameBuffer& operator=(FrameBuffer&& other) noexcept;

    /**
     * Deleted copy constructor and assignment to prevent accidental copies
     */
    FrameBuffer(const FrameBuffer&) = delete;
    FrameBuffer& operator=(const FrameBuffer&) = delete;

    /**
     * Destructor
     */
    ~FrameBuffer();

    /**
     * Get a Mat view of the buffer data (zero-copy)
     * @return cv::Mat that references the internal buffer
     */
    cv::Mat getMatView() const;

    /**
     * Get a const Mat view of the buffer data (zero-copy)
     * @return const cv::Mat that references the internal buffer
     */
    const cv::Mat getConstMatView() const;

    /**
     * Check if the buffer is valid and ready for use
     * @return true if buffer contains valid data
     */
    bool isValid() const { return m_isValid; }

    /**
     * Get the size of the buffer in bytes
     * @return Buffer size
     */
    size_t size() const { return m_size; }

    /**
     * Get raw data pointer (for advanced use cases)
     * @return Pointer to raw buffer data
     */
    uint8_t* data() const;

    /**
     * Reset the buffer to empty state
     */
    void reset();

    /**
     * Create a FrameBuffer from an existing cv::Mat (with data copy)
     * This is a convenience method for migration from Mat-based code
     * @param mat Source Mat to copy from
     * @return FrameBuffer containing a copy of the Mat data
     */
    static FrameBuffer fromMat(const cv::Mat& mat);

    /**
     * Create a FrameBuffer that wraps existing Mat data (zero-copy, dangerous)
     * WARNING: The original Mat must remain valid for the lifetime of this FrameBuffer
     * @param mat Source Mat to wrap (data not copied)
     * @return FrameBuffer that references the Mat's data
     */
    static FrameBuffer wrapMat(const cv::Mat& mat);
};

/**
 * Memory-mapped frame chunk for efficient large-scale frame storage
 * Uses memory mapping to handle large frame sequences without excessive RAM usage
 */
class MappedFrameChunk {
private:
    void* m_mappedMemory;                    // Memory-mapped region
    size_t m_mappedSize;                     // Size of mapped region
    std::vector<cv::Mat> m_frameViews;       // Mat views into mapped memory
    std::string m_tempFilePath;              // Temporary file path for mapping
#ifdef _WIN32
    HANDLE m_fileHandle;                     // Windows file handle
#else
    int m_fileDescriptor;                    // Unix file descriptor for mapping
#endif
    bool m_isValid;                          // Mapping validity flag

    // Frame layout information
    int m_frameWidth;
    int m_frameHeight;
    int m_frameType;
    size_t m_frameSize;                      // Size per frame in bytes
    size_t m_maxFrames;                      // Maximum frames that can fit

public:
    /**
     * Constructor for memory-mapped frame chunk
     * @param maxFrames Maximum number of frames to store
     * @param frameWidth Width of each frame
     * @param frameHeight Height of each frame
     * @param frameType OpenCV matrix type
     */
    MappedFrameChunk(size_t maxFrames, int frameWidth, int frameHeight, int frameType);

    /**
     * Destructor - cleans up memory mapping
     */
    ~MappedFrameChunk();

    /**
     * Deleted copy constructor and assignment
     */
    MappedFrameChunk(const MappedFrameChunk&) = delete;
    MappedFrameChunk& operator=(const MappedFrameChunk&) = delete;

    /**
     * Move constructor
     */
    MappedFrameChunk(MappedFrameChunk&& other) noexcept;

    /**
     * Move assignment operator
     */
    MappedFrameChunk& operator=(MappedFrameChunk&& other) noexcept;

    /**
     * Add a frame to the chunk (copies data into mapped memory)
     * @param frame Frame to add
     * @return true if frame was added successfully
     */
    bool addFrame(const cv::Mat& frame);

    /**
     * Add a frame from FrameBuffer (zero-copy if possible)
     * @param frameBuffer FrameBuffer to add
     * @return true if frame was added successfully
     */
    bool addFrame(const FrameBuffer& frameBuffer);

    /**
     * Get a frame view by index (zero-copy)
     * @param index Frame index
     * @return Mat view of the frame, or empty Mat if index invalid
     */
    cv::Mat getFrame(size_t index) const;

    /**
     * Get all frame views (zero-copy)
     * @return Vector of Mat views into the mapped memory
     */
    const std::vector<cv::Mat>& getFrameViews() const { return m_frameViews; }

    /**
     * Get the number of frames currently stored
     * @return Frame count
     */
    size_t frameCount() const { return m_frameViews.size(); }

    /**
     * Get the maximum number of frames that can be stored
     * @return Maximum frame capacity
     */
    size_t maxFrames() const { return m_maxFrames; }

    /**
     * Check if the chunk is valid and ready for use
     * @return true if mapping is valid
     */
    bool isValid() const { return m_isValid; }

    /**
     * Clear all frames from the chunk
     */
    void clear();

    /**
     * Get memory usage statistics
     * @return Size of mapped memory in bytes
     */
    size_t getMemoryUsage() const { return m_mappedSize; }

private:
    /**
     * Initialize the memory mapping
     * @return true if mapping was successful
     */
    bool initializeMapping();

    /**
     * Cleanup the memory mapping
     */
    void cleanupMapping();

    /**
     * Calculate the size needed for a single frame
     * @return Frame size in bytes
     */
    size_t calculateFrameSize() const;
};

/**
 * Memory pool for reusing OpenCV Mat objects
 * Reduces allocation overhead by maintaining a pool of pre-allocated matrices
 */
class MatMemoryPool {
private:
    struct PoolEntry {
        cv::Mat mat;
        bool inUse;
        std::chrono::steady_clock::time_point lastUsed;

        PoolEntry() : inUse(false) {}
    };

    std::vector<PoolEntry> m_grayBuffers;    // Pool for grayscale matrices
    std::vector<PoolEntry> m_workBuffers;    // Pool for working matrices
    mutable std::mutex m_poolMutex;          // Thread safety (mutable for const methods)
    size_t m_maxPoolSize;                    // Maximum pool size

public:
    /**
     * Constructor
     * @param maxPoolSize Maximum number of matrices to pool
     */
    explicit MatMemoryPool(size_t maxPoolSize = 50);

    /**
     * Destructor
     */
    ~MatMemoryPool();

    /**
     * Acquire a grayscale buffer from the pool
     * @param width Required width
     * @param height Required height
     * @return Mat from pool or newly allocated
     */
    cv::Mat acquireGrayBuffer(int width, int height);

    /**
     * Acquire a working buffer from the pool
     * @param width Required width
     * @param height Required height
     * @param type Required OpenCV type
     * @return Mat from pool or newly allocated
     */
    cv::Mat acquireWorkBuffer(int width, int height, int type);

    /**
     * Release a buffer back to the pool
     * @param buffer Buffer to release
     * @param isGrayBuffer true if this is a grayscale buffer
     */
    void releaseBuffer(cv::Mat& buffer, bool isGrayBuffer = true);

    /**
     * Clear unused buffers from the pool
     * @param maxAge Maximum age in seconds for unused buffers
     */
    void cleanupUnusedBuffers(int maxAge = 60);

    /**
     * Get pool statistics
     * @return Pair of (gray_pool_size, work_pool_size)
     */
    std::pair<size_t, size_t> getPoolStats() const;

private:
    /**
     * Find a suitable buffer in the pool
     * @param pool Pool to search
     * @param width Required width
     * @param height Required height
     * @param type Required type
     * @return Index of suitable buffer or -1 if none found
     */
    int findSuitableBuffer(std::vector<PoolEntry>& pool, int width, int height, int type);
};

/**
 * RAII wrapper for automatic buffer management
 * Ensures buffers are properly returned to the pool
 */
class PooledMat {
private:
    cv::Mat m_mat;
    MatMemoryPool* m_pool;
    bool m_isGrayBuffer;
    bool m_released;

public:
    /**
     * Constructor
     * @param mat Mat from pool
     * @param pool Pool to return to
     * @param isGrayBuffer Whether this is a grayscale buffer
     */
    PooledMat(cv::Mat mat, MatMemoryPool* pool, bool isGrayBuffer);

    /**
     * Destructor - automatically returns buffer to pool
     */
    ~PooledMat();

    /**
     * Move constructor
     */
    PooledMat(PooledMat&& other) noexcept;

    /**
     * Move assignment
     */
    PooledMat& operator=(PooledMat&& other) noexcept;

    /**
     * Deleted copy operations
     */
    PooledMat(const PooledMat&) = delete;
    PooledMat& operator=(const PooledMat&) = delete;

    /**
     * Get the Mat reference
     * @return Reference to the pooled Mat
     */
    cv::Mat& get() { return m_mat; }
    const cv::Mat& get() const { return m_mat; }

    /**
     * Arrow operator for direct Mat access
     */
    cv::Mat* operator->() { return &m_mat; }
    const cv::Mat* operator->() const { return &m_mat; }

    /**
     * Dereference operator
     */
    cv::Mat& operator*() { return m_mat; }
    const cv::Mat& operator*() const { return m_mat; }

    /**
     * Release the buffer early (before destructor)
     */
    void release();
};

#endif // MEMORYOPTIMIZER_H