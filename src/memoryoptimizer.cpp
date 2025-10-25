#include "memoryoptimizer.h"
#include <cstring>
#include <chrono>
#include <algorithm>
#include <QDebug>
#include <QStandardPaths>
#include <QDir>

// Platform-specific includes
#ifdef _WIN32
    #include <windows.h>
    #include <malloc.h>
#else
    #include <fcntl.h>
    #include <sys/mman.h>
    #include <unistd.h>
#endif

// ============================================================================
// AlignedAllocator Implementation
// ============================================================================

void* AlignedAllocator::allocate(size_t size, size_t alignment) {
    if (size == 0) {
        return nullptr;
    }

    // Ensure alignment is a power of 2
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        alignment = 32; // Default to 32-byte alignment
    }

#ifdef _WIN32
    return _aligned_malloc(size, alignment);
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) != 0) {
        return nullptr;
    }
    return ptr;
#endif
}

void AlignedAllocator::deallocate(void* ptr) {
    if (ptr == nullptr) {
        return;
    }

#ifdef _WIN32
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

bool AlignedAllocator::isAligned(const void* ptr, size_t alignment) {
    return (reinterpret_cast<uintptr_t>(ptr) % alignment) == 0;
}

size_t AlignedAllocator::getPageSize() {
#ifdef _WIN32
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    return sysInfo.dwPageSize;
#else
    return static_cast<size_t>(getpagesize());
#endif
}

void* AlignedAllocator::allocateLargePages(size_t size) {
#ifdef _WIN32
    // Windows large page allocation
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        return nullptr;
    }

    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    if (!LookupPrivilegeValue(nullptr, SE_LOCK_MEMORY_NAME, &tp.Privileges[0].Luid)) {
        CloseHandle(hToken);
        return nullptr;
    }

    if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), nullptr, nullptr)) {
        CloseHandle(hToken);
        return nullptr;
    }

    CloseHandle(hToken);

    // Get large page minimum size
    SIZE_T largePageMin = GetLargePageMinimum();
    if (largePageMin == 0 || size < largePageMin) {
        return nullptr;
    }

    // Align size to large page boundary
    size_t alignedSize = ((size + largePageMin - 1) / largePageMin) * largePageMin;

    return VirtualAlloc(nullptr, alignedSize, MEM_COMMIT | MEM_RESERVE | MEM_LARGE_PAGES, PAGE_READWRITE);

#elif defined(__linux__)
    // Linux huge page allocation
    size_t hugepageSize = 2 * 1024 * 1024; // 2MB huge pages
    size_t alignedSize = ((size + hugepageSize - 1) / hugepageSize) * hugepageSize;

    void* ptr = mmap(nullptr, alignedSize, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);

    if (ptr == MAP_FAILED) {
        return nullptr;
    }

    return ptr;

#elif defined(__APPLE__)
    // macOS doesn't have huge pages, fall back to regular allocation
    return allocate(size, getPageSize());

#else
    // Fallback for other platforms
    return allocate(size, getPageSize());
#endif
}

void AlignedAllocator::deallocateLargePages(void* ptr, size_t size) {
    if (!ptr) return;

#ifdef _WIN32
    VirtualFree(ptr, 0, MEM_RELEASE);
#elif defined(__linux__)
    size_t hugepageSize = 2 * 1024 * 1024; // 2MB huge pages
    size_t alignedSize = ((size + hugepageSize - 1) / hugepageSize) * hugepageSize;
    munmap(ptr, alignedSize);
#else
    deallocate(ptr);
#endif
}

// ============================================================================
// FrameBuffer Implementation
// ============================================================================

FrameBuffer::FrameBuffer()
    : m_data(nullptr, AlignedAllocator::deallocate), m_size(0), m_isValid(false) {
}

FrameBuffer::FrameBuffer(int rows, int cols, int type)
    : m_data(nullptr, AlignedAllocator::deallocate), m_size(0), m_isValid(false) {

    if (rows <= 0 || cols <= 0) {
        return;
    }

    // Calculate required buffer size
    int channels = CV_MAT_CN(type);
    int depth = CV_MAT_DEPTH(type);
    size_t elemSize = 0;

    switch (depth) {
        case CV_8U: case CV_8S: elemSize = 1; break;
        case CV_16U: case CV_16S: elemSize = 2; break;
        case CV_32S: case CV_32F: elemSize = 4; break;
        case CV_64F: elemSize = 8; break;
        default: return; // Unsupported type
    }

    m_size = rows * cols * channels * elemSize;

    // Allocate aligned memory
    uint8_t* rawPtr = static_cast<uint8_t*>(AlignedAllocator::allocate(m_size, 32));
    if (!rawPtr) {
        return;
    }

    // Create unique_ptr with custom deleter
    m_data.reset(rawPtr);

    // Create Mat wrapper (zero-copy)
    m_mat = cv::Mat(rows, cols, type, rawPtr);
    m_isValid = true;
}

FrameBuffer::FrameBuffer(uint8_t* data, int rows, int cols, int type, size_t dataSize)
    : m_data(nullptr, AlignedAllocator::deallocate), m_size(dataSize), m_isValid(false) {

    if (!data || rows <= 0 || cols <= 0 || dataSize == 0) {
        return;
    }

    // Take ownership of the data
    m_data.reset(data);

    // Create Mat wrapper (zero-copy)
    m_mat = cv::Mat(rows, cols, type, data);
    m_isValid = true;
}

FrameBuffer::FrameBuffer(FrameBuffer&& other) noexcept
    : m_data(std::move(other.m_data)), m_mat(std::move(other.m_mat)),
      m_size(other.m_size), m_isValid(other.m_isValid) {

    other.m_size = 0;
    other.m_isValid = false;
}

FrameBuffer& FrameBuffer::operator=(FrameBuffer&& other) noexcept {
    if (this != &other) {
        m_data = std::move(other.m_data);
        m_mat = std::move(other.m_mat);
        m_size = other.m_size;
        m_isValid = other.m_isValid;

        other.m_size = 0;
        other.m_isValid = false;
    }
    return *this;
}

FrameBuffer::~FrameBuffer() {
    reset();
}

cv::Mat FrameBuffer::getMatView() const {
    return m_mat;
}

const cv::Mat FrameBuffer::getConstMatView() const {
    return m_mat;
}

uint8_t* FrameBuffer::data() const {
    return m_data.get();
}

void FrameBuffer::reset() {
    m_data.reset();
    m_mat = cv::Mat();
    m_size = 0;
    m_isValid = false;
}

FrameBuffer FrameBuffer::fromMat(const cv::Mat& mat) {
    if (mat.empty()) {
        return FrameBuffer();
    }

    FrameBuffer buffer(mat.rows, mat.cols, mat.type());
    if (buffer.isValid()) {
        mat.copyTo(buffer.getMatView());
    }

    return buffer;
}

FrameBuffer FrameBuffer::wrapMat(const cv::Mat& mat) {
    if (mat.empty() || !mat.isContinuous()) {
        return FrameBuffer();
    }

    // WARNING: This creates a FrameBuffer that doesn't own the data
    // The original Mat must remain valid for the lifetime of this FrameBuffer
    FrameBuffer buffer;
    buffer.m_mat = mat; // Shallow copy - shares data
    buffer.m_size = mat.total() * mat.elemSize();
    buffer.m_isValid = true;
    // Note: m_data remains nullptr since we don't own the data

    return buffer;
}

// ============================================================================
// MappedFrameChunk Implementation
// ============================================================================

MappedFrameChunk::MappedFrameChunk(size_t maxFrames, int frameWidth, int frameHeight, int frameType)
    : m_mappedMemory(nullptr), m_mappedSize(0), m_tempFilePath(),
#ifdef _WIN32
      m_fileHandle(INVALID_HANDLE_VALUE),
#else
      m_fileDescriptor(-1),
#endif
      m_isValid(false), m_frameWidth(frameWidth), m_frameHeight(frameHeight),
      m_frameType(frameType), m_frameSize(0), m_maxFrames(maxFrames) {

    if (maxFrames == 0 || frameWidth <= 0 || frameHeight <= 0) {
        return;
    }

    m_frameSize = calculateFrameSize();
    if (m_frameSize == 0) {
        return;
    }

    m_mappedSize = m_frameSize * maxFrames;

    if (initializeMapping()) {
        m_frameViews.reserve(maxFrames);
        m_isValid = true;
    }
}

MappedFrameChunk::~MappedFrameChunk() {
    cleanupMapping();
}

MappedFrameChunk::MappedFrameChunk(MappedFrameChunk&& other) noexcept
    : m_mappedMemory(other.m_mappedMemory), m_mappedSize(other.m_mappedSize),
      m_frameViews(std::move(other.m_frameViews)), m_tempFilePath(std::move(other.m_tempFilePath)),
#ifdef _WIN32
      m_fileHandle(other.m_fileHandle),
#else
      m_fileDescriptor(other.m_fileDescriptor),
#endif
      m_isValid(other.m_isValid),
      m_frameWidth(other.m_frameWidth), m_frameHeight(other.m_frameHeight),
      m_frameType(other.m_frameType), m_frameSize(other.m_frameSize),
      m_maxFrames(other.m_maxFrames) {

    // Reset other object
    other.m_mappedMemory = nullptr;
    other.m_mappedSize = 0;
#ifdef _WIN32
    other.m_fileHandle = INVALID_HANDLE_VALUE;
#else
    other.m_fileDescriptor = -1;
#endif
    other.m_isValid = false;
    other.m_frameSize = 0;
    other.m_maxFrames = 0;
}

MappedFrameChunk& MappedFrameChunk::operator=(MappedFrameChunk&& other) noexcept {
    if (this != &other) {
        // Cleanup current state
        cleanupMapping();

        // Move from other
        m_mappedMemory = other.m_mappedMemory;
        m_mappedSize = other.m_mappedSize;
        m_frameViews = std::move(other.m_frameViews);
        m_tempFilePath = std::move(other.m_tempFilePath);
#ifdef _WIN32
        m_fileHandle = other.m_fileHandle;
#else
        m_fileDescriptor = other.m_fileDescriptor;
#endif
        m_isValid = other.m_isValid;
        m_frameWidth = other.m_frameWidth;
        m_frameHeight = other.m_frameHeight;
        m_frameType = other.m_frameType;
        m_frameSize = other.m_frameSize;
        m_maxFrames = other.m_maxFrames;

        // Reset other
        other.m_mappedMemory = nullptr;
        other.m_mappedSize = 0;
#ifdef _WIN32
        other.m_fileHandle = INVALID_HANDLE_VALUE;
#else
        other.m_fileDescriptor = -1;
#endif
        other.m_isValid = false;
        other.m_frameSize = 0;
        other.m_maxFrames = 0;
    }
    return *this;
}

bool MappedFrameChunk::addFrame(const cv::Mat& frame) {
    if (!m_isValid || m_frameViews.size() >= m_maxFrames) {
        return false;
    }

    if (frame.rows != m_frameHeight || frame.cols != m_frameWidth || frame.type() != m_frameType) {
        return false;
    }

    // Calculate offset for this frame
    size_t frameIndex = m_frameViews.size();
    uint8_t* framePtr = static_cast<uint8_t*>(m_mappedMemory) + (frameIndex * m_frameSize);

    // Create Mat view into mapped memory
    cv::Mat frameView(m_frameHeight, m_frameWidth, m_frameType, framePtr);

    // Copy frame data
    frame.copyTo(frameView);

    // Add to frame views
    m_frameViews.push_back(frameView);

    return true;
}

bool MappedFrameChunk::addFrame(const FrameBuffer& frameBuffer) {
    if (!frameBuffer.isValid()) {
        return false;
    }

    return addFrame(frameBuffer.getConstMatView());
}

cv::Mat MappedFrameChunk::getFrame(size_t index) const {
    if (index >= m_frameViews.size()) {
        return cv::Mat();
    }

    return m_frameViews[index];
}

void MappedFrameChunk::clear() {
    m_frameViews.clear();
}

bool MappedFrameChunk::initializeMapping() {
    // Create temporary file for memory mapping
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QDir().mkpath(tempDir);

#ifdef _WIN32
    // Windows implementation using CreateFileMapping
    QString fileName = QString("autoslides_chunk_%1_%2")
        .arg(GetCurrentProcessId())
        .arg(GetTickCount64());
    QString fullPath = QDir(tempDir).filePath(fileName);
    m_tempFilePath = fullPath.toUtf8().toStdString();

    // Create temporary file - use QString's toStdWString for proper UTF-16 conversion
    std::wstring wTempPath = fullPath.toStdWString();
    HANDLE hFile = CreateFileW(
        wTempPath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
        nullptr
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        qDebug() << "Failed to create temporary file for memory mapping";
        return false;
    }

    // Set file size
    LARGE_INTEGER fileSize;
    fileSize.QuadPart = m_mappedSize;
    if (!SetFilePointerEx(hFile, fileSize, nullptr, FILE_BEGIN) || !SetEndOfFile(hFile)) {
        qDebug() << "Failed to resize temporary file";
        CloseHandle(hFile);
        return false;
    }

    // Create file mapping
    HANDLE hMapping = CreateFileMappingW(
        hFile,
        nullptr,
        PAGE_READWRITE,
        fileSize.HighPart,
        fileSize.LowPart,
        nullptr
    );

    if (!hMapping) {
        qDebug() << "Failed to create file mapping";
        CloseHandle(hFile);
        return false;
    }

    // Map view of file
    m_mappedMemory = MapViewOfFile(
        hMapping,
        FILE_MAP_ALL_ACCESS,
        0,
        0,
        m_mappedSize
    );

    if (!m_mappedMemory) {
        qDebug() << "Failed to map view of file";
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return false;
    }

    // Store handles for cleanup
    m_fileHandle = hFile;
    // Note: We should store hMapping separately, but for simplicity we'll close it here
    // since the mapping will remain valid until UnmapViewOfFile is called
    CloseHandle(hMapping);

#else
    // Unix/Linux/macOS implementation using mmap
    m_tempFilePath = QDir(tempDir).filePath("autoslides_chunk_XXXXXX").toStdString();

    // Create temporary file
    std::vector<char> templateStr(m_tempFilePath.begin(), m_tempFilePath.end());
    templateStr.push_back('\0');

    m_fileDescriptor = mkstemp(templateStr.data());
    if (m_fileDescriptor == -1) {
        qDebug() << "Failed to create temporary file for memory mapping";
        return false;
    }

    m_tempFilePath = std::string(templateStr.data());

    // Resize file to required size
    if (ftruncate(m_fileDescriptor, m_mappedSize) == -1) {
        qDebug() << "Failed to resize temporary file";
        close(m_fileDescriptor);
        m_fileDescriptor = -1;
        return false;
    }

    // Create memory mapping
    m_mappedMemory = mmap(nullptr, m_mappedSize, PROT_READ | PROT_WRITE, MAP_SHARED, m_fileDescriptor, 0);
    if (m_mappedMemory == MAP_FAILED) {
        qDebug() << "Failed to create memory mapping";
        m_mappedMemory = nullptr;
        close(m_fileDescriptor);
        m_fileDescriptor = -1;
        return false;
    }
#endif

    return true;
}

void MappedFrameChunk::cleanupMapping() {
    // Clear frame views first
    m_frameViews.clear();

#ifdef _WIN32
    // Windows cleanup
    if (m_mappedMemory) {
        UnmapViewOfFile(m_mappedMemory);
        m_mappedMemory = nullptr;
    }

    if (m_fileHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_fileHandle);
        m_fileHandle = INVALID_HANDLE_VALUE;
    }

    // Remove temporary file (Windows will auto-delete due to FILE_FLAG_DELETE_ON_CLOSE)
    m_tempFilePath.clear();

#else
    // Unix/Linux/macOS cleanup
    if (m_mappedMemory && m_mappedMemory != MAP_FAILED) {
        munmap(m_mappedMemory, m_mappedSize);
        m_mappedMemory = nullptr;
    }

    if (m_fileDescriptor != -1) {
        close(m_fileDescriptor);
        m_fileDescriptor = -1;
    }

    // Remove temporary file
    if (!m_tempFilePath.empty()) {
        unlink(m_tempFilePath.c_str());
        m_tempFilePath.clear();
    }
#endif

    m_mappedSize = 0;
    m_isValid = false;
}

size_t MappedFrameChunk::calculateFrameSize() const {
    int channels = CV_MAT_CN(m_frameType);
    int depth = CV_MAT_DEPTH(m_frameType);
    size_t elemSize = 0;

    switch (depth) {
        case CV_8U: case CV_8S: elemSize = 1; break;
        case CV_16U: case CV_16S: elemSize = 2; break;
        case CV_32S: case CV_32F: elemSize = 4; break;
        case CV_64F: elemSize = 8; break;
        default: return 0;
    }

    return m_frameWidth * m_frameHeight * channels * elemSize;
}

// ============================================================================
// MatMemoryPool Implementation
// ============================================================================

MatMemoryPool::MatMemoryPool(size_t maxPoolSize) : m_maxPoolSize(maxPoolSize) {
    m_grayBuffers.reserve(maxPoolSize / 2);
    m_workBuffers.reserve(maxPoolSize / 2);
}

MatMemoryPool::~MatMemoryPool() {
    std::lock_guard<std::mutex> lock(m_poolMutex);
    m_grayBuffers.clear();
    m_workBuffers.clear();
}

cv::Mat MatMemoryPool::acquireGrayBuffer(int width, int height) {
    std::lock_guard<std::mutex> lock(m_poolMutex);

    int index = findSuitableBuffer(m_grayBuffers, width, height, CV_8UC1);
    if (index >= 0) {
        m_grayBuffers[index].inUse = true;
        m_grayBuffers[index].lastUsed = std::chrono::steady_clock::now();
        return m_grayBuffers[index].mat;
    }

    // Create new buffer if pool not full
    if (m_grayBuffers.size() < m_maxPoolSize / 2) {
        PoolEntry entry;
        entry.mat = cv::Mat(height, width, CV_8UC1);
        entry.inUse = true;
        entry.lastUsed = std::chrono::steady_clock::now();
        m_grayBuffers.push_back(entry);
        return entry.mat;
    }

    // Pool full, create temporary buffer
    return cv::Mat(height, width, CV_8UC1);
}

cv::Mat MatMemoryPool::acquireWorkBuffer(int width, int height, int type) {
    std::lock_guard<std::mutex> lock(m_poolMutex);

    int index = findSuitableBuffer(m_workBuffers, width, height, type);
    if (index >= 0) {
        m_workBuffers[index].inUse = true;
        m_workBuffers[index].lastUsed = std::chrono::steady_clock::now();
        return m_workBuffers[index].mat;
    }

    // Create new buffer if pool not full
    if (m_workBuffers.size() < m_maxPoolSize / 2) {
        PoolEntry entry;
        entry.mat = cv::Mat(height, width, type);
        entry.inUse = true;
        entry.lastUsed = std::chrono::steady_clock::now();
        m_workBuffers.push_back(entry);
        return entry.mat;
    }

    // Pool full, create temporary buffer
    return cv::Mat(height, width, type);
}

void MatMemoryPool::releaseBuffer(cv::Mat& buffer, bool isGrayBuffer) {
    if (buffer.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_poolMutex);

    auto& pool = isGrayBuffer ? m_grayBuffers : m_workBuffers;

    // Find the buffer in the pool
    for (auto& entry : pool) {
        if (entry.mat.data == buffer.data) {
            entry.inUse = false;
            entry.lastUsed = std::chrono::steady_clock::now();
            break;
        }
    }

    // Clear the reference
    buffer = cv::Mat();
}

void MatMemoryPool::cleanupUnusedBuffers(int maxAge) {
    std::lock_guard<std::mutex> lock(m_poolMutex);

    auto now = std::chrono::steady_clock::now();
    auto maxAgeSeconds = std::chrono::seconds(maxAge);

    auto cleanupPool = [&](std::vector<PoolEntry>& pool) {
        pool.erase(std::remove_if(pool.begin(), pool.end(), [&](const PoolEntry& entry) {
            return !entry.inUse && (now - entry.lastUsed) > maxAgeSeconds;
        }), pool.end());
    };

    cleanupPool(m_grayBuffers);
    cleanupPool(m_workBuffers);
}

std::pair<size_t, size_t> MatMemoryPool::getPoolStats() const {
    std::lock_guard<std::mutex> lock(m_poolMutex);
    return {m_grayBuffers.size(), m_workBuffers.size()};
}

int MatMemoryPool::findSuitableBuffer(std::vector<PoolEntry>& pool, int width, int height, int type) {
    for (size_t i = 0; i < pool.size(); ++i) {
        if (!pool[i].inUse &&
            pool[i].mat.cols >= width &&
            pool[i].mat.rows >= height &&
            pool[i].mat.type() == type) {

            // Resize if necessary (this doesn't reallocate if smaller)
            if (pool[i].mat.cols != width || pool[i].mat.rows != height) {
                pool[i].mat = pool[i].mat(cv::Rect(0, 0, width, height));
            }

            return static_cast<int>(i);
        }
    }
    return -1;
}

// ============================================================================
// PooledMat Implementation
// ============================================================================

PooledMat::PooledMat(cv::Mat mat, MatMemoryPool* pool, bool isGrayBuffer)
    : m_mat(mat), m_pool(pool), m_isGrayBuffer(isGrayBuffer), m_released(false) {
}

PooledMat::~PooledMat() {
    release();
}

PooledMat::PooledMat(PooledMat&& other) noexcept
    : m_mat(std::move(other.m_mat)), m_pool(other.m_pool),
      m_isGrayBuffer(other.m_isGrayBuffer), m_released(other.m_released) {

    other.m_pool = nullptr;
    other.m_released = true;
}

PooledMat& PooledMat::operator=(PooledMat&& other) noexcept {
    if (this != &other) {
        release();

        m_mat = std::move(other.m_mat);
        m_pool = other.m_pool;
        m_isGrayBuffer = other.m_isGrayBuffer;
        m_released = other.m_released;

        other.m_pool = nullptr;
        other.m_released = true;
    }
    return *this;
}

void PooledMat::release() {
    if (!m_released && m_pool) {
        m_pool->releaseBuffer(m_mat, m_isGrayBuffer);
        m_released = true;
    }
}