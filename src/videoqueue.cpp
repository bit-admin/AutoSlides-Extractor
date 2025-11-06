#include "videoqueue.h"
#include <QFileInfo>
#include <algorithm>

VideoQueue::VideoQueue(QObject *parent)
    : QObject(parent)
{
}

int VideoQueue::addVideo(const QString& filePath)
{
    // Check if file exists
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        return -1;
    }

    // Check if video is already in queue
    for (const auto& video : m_videos) {
        if (video.filePath == filePath) {
            return -1; // Already exists
        }
    }

    // Add to queue
    m_videos.emplace_back(filePath);
    int index = static_cast<int>(m_videos.size() - 1);

    emit videoAdded(index);
    return index;
}

bool VideoQueue::removeVideo(int index)
{
    if (index < 0 || index >= static_cast<int>(m_videos.size())) {
        return false;
    }

    // Don't allow removal of currently processing videos
    ProcessingStatus status = m_videos[index].status;
    if (status == ProcessingStatus::FFmpegHandling ||
        status == ProcessingStatus::SSIMCalculating ||
        status == ProcessingStatus::ImageProcessing) {
        return false;
    }

    m_videos.erase(m_videos.begin() + index);
    emit videoRemoved(index);
    return true;
}

VideoQueueItem* VideoQueue::getVideo(int index)
{
    if (index < 0 || index >= static_cast<int>(m_videos.size())) {
        return nullptr;
    }
    return &m_videos[index];
}

const std::vector<VideoQueueItem>& VideoQueue::getAllVideos() const
{
    return m_videos;
}

int VideoQueue::size() const
{
    return static_cast<int>(m_videos.size());
}

bool VideoQueue::isEmpty() const
{
    return m_videos.empty();
}

void VideoQueue::updateStatus(int index, ProcessingStatus status)
{
    if (index < 0 || index >= static_cast<int>(m_videos.size())) {
        return;
    }

    VideoQueueItem& video = m_videos[index];
    video.status = status;

    // Update timestamps
    if (status == ProcessingStatus::FFmpegHandling && video.startTime.isNull()) {
        video.startTime = QDateTime::currentDateTime();
    } else if (status == ProcessingStatus::Completed || status == ProcessingStatus::Error) {
        video.endTime = QDateTime::currentDateTime();
        if (!video.startTime.isNull()) {
            video.processingTimeSeconds = video.startTime.msecsTo(video.endTime) / 1000.0;
        }
    }

    emit statusChanged(index, status);
}

void VideoQueue::updateStatistics(int index, int extractedSlides, double processingTime)
{
    if (index < 0 || index >= static_cast<int>(m_videos.size())) {
        return;
    }

    VideoQueueItem& video = m_videos[index];
    video.extractedSlides = extractedSlides;
    if (processingTime > 0) {
        video.processingTimeSeconds = processingTime;
    }

    emit statisticsUpdated(index);
}

void VideoQueue::setError(int index, const QString& errorMessage)
{
    if (index < 0 || index >= static_cast<int>(m_videos.size())) {
        return;
    }

    VideoQueueItem& video = m_videos[index];
    video.status = ProcessingStatus::Error;
    video.errorMessage = errorMessage;
    video.endTime = QDateTime::currentDateTime();

    if (!video.startTime.isNull()) {
        video.processingTimeSeconds = video.startTime.msecsTo(video.endTime) / 1000.0;
    }

    emit statusChanged(index, ProcessingStatus::Error);
}

void VideoQueue::clearCompleted()
{
    auto it = std::remove_if(m_videos.begin(), m_videos.end(),
        [](const VideoQueueItem& video) {
            return video.status == ProcessingStatus::Completed ||
                   video.status == ProcessingStatus::Error;
        });

    if (it != m_videos.end()) {
        m_videos.erase(it, m_videos.end());
        emit queueCleared();
    }
}

void VideoQueue::clearAll()
{
    // Only clear if no videos are currently processing
    if (!isProcessing()) {
        m_videos.clear();
        emit queueCleared();
    }
}

int VideoQueue::getNextToProcess() const
{
    for (int i = 0; i < static_cast<int>(m_videos.size()); ++i) {
        if (m_videos[i].status == ProcessingStatus::Queued) {
            return i;
        }
    }
    return -1;
}

bool VideoQueue::isProcessing() const
{
    for (const auto& video : m_videos) {
        ProcessingStatus status = video.status;
        if (status == ProcessingStatus::FFmpegHandling ||
            status == ProcessingStatus::SSIMCalculating ||
            status == ProcessingStatus::ImageProcessing) {
            return true;
        }
    }
    return false;
}

void VideoQueue::resetErrorVideos()
{
    for (auto& video : m_videos) {
        if (video.status == ProcessingStatus::Error) {
            video.status = ProcessingStatus::Queued;
            video.errorMessage.clear();
            video.startTime = QDateTime();
            video.endTime = QDateTime();
            video.processingTimeSeconds = 0.0;
            emit statusChanged(static_cast<int>(&video - &m_videos[0]), ProcessingStatus::Queued);
        }
    }
}

QString VideoQueue::getStatusString(ProcessingStatus status)
{
    switch (status) {
        case ProcessingStatus::Queued:
            return "Queued";
        case ProcessingStatus::FFmpegHandling:
            return "FFmpeg Handling";
        case ProcessingStatus::SSIMCalculating:
            return "SSIM Calculating";
        case ProcessingStatus::ImageProcessing:
            return "Image Processing";
        case ProcessingStatus::Completed:
            return "Completed";
        case ProcessingStatus::Error:
            return "Error";
        default:
            return "Unknown";
    }
}