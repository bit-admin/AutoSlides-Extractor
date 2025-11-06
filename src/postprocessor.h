#ifndef POSTPROCESSOR_H
#define POSTPROCESSOR_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <vector>
#include "phashcalculator.h"

/**
 * @brief Structure to hold exclusion list entry
 */
struct ExclusionEntry {
    QString remark;
    QString hashHex;
    std::vector<uint8_t> hashBytes;

    ExclusionEntry() = default;
    ExclusionEntry(const QString& rem, const QString& hex)
        : remark(rem), hashHex(hex), hashBytes(PHashCalculator::hexStringToHash(hex)) {}
};

/**
 * @brief Post-processor for slide images
 *
 * Handles post-processing of extracted slide images including:
 * - Duplicate detection using pHash
 * - Exclusion list comparison
 * - Moving duplicates to trash
 */
class PostProcessor : public QObject
{
    Q_OBJECT

public:
    explicit PostProcessor(QObject *parent = nullptr);

    /**
     * @brief Process a directory of images
     * @param imageDir Directory containing images to process
     * @param deleteRedundant Enable duplicate detection and removal
     * @param compareExcluded Enable exclusion list comparison
     * @param hammingThreshold Hamming distance threshold for similarity
     * @param exclusionList List of excluded hashes
     * @return Number of images moved to trash
     */
    int processDirectory(const QString& imageDir,
                        bool deleteRedundant,
                        bool compareExcluded,
                        int hammingThreshold,
                        const QList<ExclusionEntry>& exclusionList);

    /**
     * @brief Get list of images that were moved to trash
     * @return List of file paths moved to trash
     */
    QStringList getMovedToTrash() const { return m_movedToTrash; }

    /**
     * @brief Get total number of images processed
     * @return Number of images processed
     */
    int getTotalProcessed() const { return m_totalProcessed; }

    /**
     * @brief Get default exclusion list (preset hashes)
     * @return List of default exclusion entries
     */
    static QList<ExclusionEntry> getDefaultExclusionList();

signals:
    /**
     * @brief Emitted when processing progress updates
     * @param current Current image index
     * @param total Total number of images
     */
    void progressUpdated(int current, int total);

    /**
     * @brief Emitted when an image is moved to trash
     * @param filePath Path of the image moved to trash
     * @param reason Reason for moving (duplicate or excluded)
     */
    void imageMovedToTrash(const QString& filePath, const QString& reason);

    /**
     * @brief Emitted when processing is complete
     * @param totalProcessed Total images processed
     * @param movedToTrash Number of images moved to trash
     */
    void processingComplete(int totalProcessed, int movedToTrash);

private:
    /**
     * @brief Calculate pHash for all images in directory
     * @param imageFiles List of image file paths
     * @return Map of file path to pHash
     */
    QMap<QString, std::vector<uint8_t>> calculateHashes(const QStringList& imageFiles);

    /**
     * @brief Find and remove duplicate images
     * @param imageHashes Map of file path to pHash
     * @param hammingThreshold Hamming distance threshold
     * @return List of files moved to trash
     */
    QStringList removeDuplicates(const QMap<QString, std::vector<uint8_t>>& imageHashes,
                                 int hammingThreshold);

    /**
     * @brief Find and remove images matching exclusion list
     * @param imageHashes Map of file path to pHash
     * @param exclusionList List of excluded hashes
     * @param hammingThreshold Hamming distance threshold
     * @return List of files moved to trash
     */
    QStringList removeExcluded(const QMap<QString, std::vector<uint8_t>>& imageHashes,
                               const QList<ExclusionEntry>& exclusionList,
                               int hammingThreshold);

    QStringList m_movedToTrash;
    int m_totalProcessed;
};

#endif // POSTPROCESSOR_H
