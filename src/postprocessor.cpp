#include "postprocessor.h"
#include "trashmanager.h"
#include <QDir>
#include <QFileInfo>

PostProcessor::PostProcessor(QObject *parent)
    : QObject(parent), m_totalProcessed(0)
{
}

int PostProcessor::processDirectory(const QString& imageDir,
                                    bool deleteRedundant,
                                    bool compareExcluded,
                                    int hammingThreshold,
                                    const QList<ExclusionEntry>& exclusionList)
{
    m_movedToTrash.clear();
    m_totalProcessed = 0;

    // Get list of image files
    QDir dir(imageDir);
    QStringList filters;
    filters << "*.jpg" << "*.jpeg" << "*.png" << "*.bmp";
    QStringList imageFiles = dir.entryList(filters, QDir::Files, QDir::Name);

    if (imageFiles.isEmpty()) {
        emit processingComplete(0, 0);
        return 0;
    }

    // Convert to absolute paths
    for (QString& file : imageFiles) {
        file = dir.absoluteFilePath(file);
    }

    m_totalProcessed = imageFiles.size();

    // Calculate pHash for all images
    emit progressUpdated(0, imageFiles.size());
    QMap<QString, std::vector<uint8_t>> imageHashes = calculateHashes(imageFiles);

    // Remove duplicates if enabled
    if (deleteRedundant) {
        QStringList duplicates = removeDuplicates(imageHashes, hammingThreshold);
        m_movedToTrash.append(duplicates);

        // Remove moved files from hash map
        for (const QString& file : duplicates) {
            imageHashes.remove(file);
        }
    }

    // Remove excluded images if enabled
    if (compareExcluded && !exclusionList.isEmpty()) {
        QStringList excluded = removeExcluded(imageHashes, exclusionList, hammingThreshold);
        m_movedToTrash.append(excluded);
    }

    emit processingComplete(m_totalProcessed, m_movedToTrash.size());
    return m_movedToTrash.size();
}

QMap<QString, std::vector<uint8_t>> PostProcessor::calculateHashes(const QStringList& imageFiles)
{
    QMap<QString, std::vector<uint8_t>> hashes;
    int current = 0;

    for (const QString& filePath : imageFiles) {
        std::vector<uint8_t> hash = PHashCalculator::calculatePHash(filePath);
        if (!hash.empty()) {
            hashes[filePath] = hash;
        }
        current++;
        emit progressUpdated(current, imageFiles.size());
    }

    return hashes;
}

QStringList PostProcessor::removeDuplicates(const QMap<QString, std::vector<uint8_t>>& imageHashes,
                                           int hammingThreshold)
{
    QStringList movedFiles;
    QStringList processedFiles = imageHashes.keys();

    // Compare each image with subsequent images
    for (int i = 0; i < processedFiles.size(); i++) {
        const QString& file1 = processedFiles[i];

        // Skip if already moved to trash
        if (movedFiles.contains(file1)) {
            continue;
        }

        const std::vector<uint8_t>& hash1 = imageHashes[file1];

        for (int j = i + 1; j < processedFiles.size(); j++) {
            const QString& file2 = processedFiles[j];

            // Skip if already moved to trash
            if (movedFiles.contains(file2)) {
                continue;
            }

            const std::vector<uint8_t>& hash2 = imageHashes[file2];

            // Calculate Hamming distance
            int distance = PHashCalculator::hammingDistance(hash1, hash2);

            if (distance >= 0 && distance <= hammingThreshold) {
                // Images are similar - move the later one to trash
                if (TrashManager::moveToTrash(file2)) {
                    movedFiles.append(file2);
                    emit imageMovedToTrash(file2, QString("Duplicate (distance: %1)").arg(distance));
                }
            }
        }
    }

    return movedFiles;
}

QStringList PostProcessor::removeExcluded(const QMap<QString, std::vector<uint8_t>>& imageHashes,
                                         const QList<ExclusionEntry>& exclusionList,
                                         int hammingThreshold)
{
    QStringList movedFiles;

    for (auto it = imageHashes.constBegin(); it != imageHashes.constEnd(); ++it) {
        const QString& filePath = it.key();
        const std::vector<uint8_t>& imageHash = it.value();

        // Compare with each entry in exclusion list
        for (const ExclusionEntry& entry : exclusionList) {
            if (entry.hashBytes.empty()) {
                continue;
            }

            int distance = PHashCalculator::hammingDistance(imageHash, entry.hashBytes);

            if (distance >= 0 && distance <= hammingThreshold) {
                // Image matches exclusion list - move to trash
                if (TrashManager::moveToTrash(filePath)) {
                    movedFiles.append(filePath);
                    emit imageMovedToTrash(filePath,
                        QString("Excluded: %1 (distance: %2)").arg(entry.remark).arg(distance));
                }
                break;  // No need to check other exclusion entries
            }
        }
    }

    return movedFiles;
}

QList<ExclusionEntry> PostProcessor::getDefaultExclusionList()
{
    QList<ExclusionEntry> list;

    // Add preset hashes (OpenCV-based pHash)
    ExclusionEntry entry1("No_Signal_1", "99c799ce6638663399c799ce6638663199c799ce6638663199c799ce66386630");
    ExclusionEntry entry2("No_Signal_2", "2ddb2658d224d1a72ddb2e58d264d1a7299b2f58d664d4a7299b091ad664f6e4");

    // Only add entries with valid hash bytes
    if (!entry1.hashBytes.empty()) {
        list.append(entry1);
    }

    if (!entry2.hashBytes.empty()) {
        list.append(entry2);
    }

    return list;
}
