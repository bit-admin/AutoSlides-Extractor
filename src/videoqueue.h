#ifndef VIDEOQUEUE_H
#define VIDEOQUEUE_H

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QFileInfo>
#include <vector>

enum class ProcessingStatus {
    Queued,
    FFmpegHandling,
    SSIMCalculating,
    ImageProcessing,
    Completed,
    Error
};

struct VideoQueueItem {
    QString filePath;
    QString fileName;
    ProcessingStatus status;
    QDateTime addedTime;
    QDateTime startTime;
    QDateTime endTime;
    int extractedSlides;
    QString errorMessage;
    double processingTimeSeconds;

    // Post-processing statistics
    int movedToTrash;
    QString outputDirectory;

    VideoQueueItem(const QString& path) :
        filePath(path),
        fileName(QFileInfo(path).fileName()),
        status(ProcessingStatus::Queued),
        addedTime(QDateTime::currentDateTime()),
        extractedSlides(0),
        processingTimeSeconds(0.0),
        movedToTrash(0)
    {}
};

class VideoQueue : public QObject
{
    Q_OBJECT

public:
    explicit VideoQueue(QObject *parent = nullptr);

    /**
     * Add video file to the queue
     * @param filePath Path to video file
     * @return Index of added item, -1 if failed
     */
    int addVideo(const QString& filePath);

    /**
     * Remove video from queue by index
     * @param index Index of video to remove
     * @return true if successful
     */
    bool removeVideo(int index);

    /**
     * Get video item by index
     * @param index Index of video
     * @return Pointer to VideoQueueItem, nullptr if invalid index
     */
    VideoQueueItem* getVideo(int index);

    /**
     * Get all videos in queue
     * @return Vector of all video items
     */
    const std::vector<VideoQueueItem>& getAllVideos() const;

    /**
     * Get number of videos in queue
     * @return Queue size
     */
    int size() const;

    /**
     * Check if queue is empty
     * @return true if empty
     */
    bool isEmpty() const;

    /**
     * Update video status
     * @param index Index of video
     * @param status New status
     */
    void updateStatus(int index, ProcessingStatus status);

    /**
     * Update video processing statistics
     * @param index Index of video
     * @param extractedSlides Number of extracted slides
     * @param processingTime Processing time in seconds
     */
    void updateStatistics(int index, int extractedSlides, double processingTime);

    /**
     * Set error message for video
     * @param index Index of video
     * @param errorMessage Error message
     */
    void setError(int index, const QString& errorMessage);

    /**
     * Clear all completed and error items from queue
     */
    void clearCompleted();

    /**
     * Clear entire queue
     */
    void clearAll();

    /**
     * Get next video to process (first queued item)
     * @return Index of next video, -1 if none available
     */
    int getNextToProcess() const;

    /**
     * Check if any video is currently being processed
     * @return true if processing
     */
    bool isProcessing() const;

    /**
     * Reset error videos back to queued status for retry
     */
    void resetErrorVideos();

    /**
     * Get status as string
     * @param status Processing status
     * @return Status string
     */
    static QString getStatusString(ProcessingStatus status);

signals:
    void videoAdded(int index);
    void videoRemoved(int index);
    void statusChanged(int index, ProcessingStatus status);
    void statisticsUpdated(int index);
    void queueCleared();

private:
    std::vector<VideoQueueItem> m_videos;
};

#endif // VIDEOQUEUE_H