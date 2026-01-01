#include "postprocessor.h"
#include "trashmanager.h"
#include "mlclassifier.h"
#include <QDir>
#include <QFileInfo>
#include <QDebug>

PostProcessor::PostProcessor(QObject *parent)
    : QObject(parent), m_totalProcessed(0)
{
}

PostProcessingResult PostProcessor::processDirectory(const QString& imageDir,
                                                    bool deleteRedundant,
                                                    bool compareExcluded,
                                                    int hammingThreshold,
                                                    const QList<ExclusionEntry>& exclusionList,
                                                    bool enableMLClassification,
                                                    const QString& mlModelPath,
                                                    float mlNotSlideHighThreshold,
                                                    float mlNotSlideLowThreshold,
                                                    float mlMaybeSlideHighThreshold,
                                                    float mlMaybeSlideLowThreshold,
                                                    float mlSlideMaxThreshold,
                                                    bool mlDeleteMaybeSlides,
                                                    const QString& mlExecutionProvider,
                                                    bool useApplicationTrash,
                                                    const QString& baseOutputDir)
{
    m_movedToTrash.clear();
    m_totalProcessed = 0;

    PostProcessingResult result;

    // Get list of image files
    QDir dir(imageDir);
    QStringList filters;
    filters << "*.jpg" << "*.jpeg" << "*.png" << "*.bmp";
    QStringList imageFiles = dir.entryList(filters, QDir::Files, QDir::Name);

    if (imageFiles.isEmpty()) {
        emit processingComplete(0, 0);
        return result;
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
        QStringList duplicates = removeDuplicates(imageHashes, hammingThreshold,
                                                  useApplicationTrash, baseOutputDir);
        m_movedToTrash.append(duplicates);
        result.removedByPHash += duplicates.size();

        // Remove moved files from hash map
        for (const QString& file : duplicates) {
            imageHashes.remove(file);
        }
    }

    // Remove excluded images if enabled
    if (compareExcluded && !exclusionList.isEmpty()) {
        QStringList excluded = removeExcluded(imageHashes, exclusionList, hammingThreshold,
                                              useApplicationTrash, baseOutputDir);
        m_movedToTrash.append(excluded);
        result.removedByPHash += excluded.size();

        // Remove moved files from hash map
        for (const QString& file : excluded) {
            imageHashes.remove(file);
        }
    }

    // ML classification if enabled
    if (enableMLClassification && MLClassifier::isAvailable()) {
        QStringList mlRemoved = classifyAndRemove(imageHashes, mlModelPath,
                                                  mlNotSlideHighThreshold,
                                                  mlNotSlideLowThreshold,
                                                  mlMaybeSlideHighThreshold,
                                                  mlMaybeSlideLowThreshold,
                                                  mlSlideMaxThreshold,
                                                  mlDeleteMaybeSlides,
                                                  mlExecutionProvider, useApplicationTrash, baseOutputDir);
        m_movedToTrash.append(mlRemoved);
        result.removedByML = mlRemoved.size();
    }

    result.totalRemoved = m_movedToTrash.size();

    emit processingComplete(m_totalProcessed, result.totalRemoved);
    return result;
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
                                           int hammingThreshold,
                                           bool useApplicationTrash,
                                           const QString& baseOutputDir)
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
                // Images are similar - move to trash
                bool success = false;
                if (useApplicationTrash) {
                    success = TrashManager::moveToApplicationTrash(file2, baseOutputDir, "phash",
                                                                   QString("Duplicate (distance: %1)").arg(distance));
                } else {
                    success = TrashManager::renameAndMoveToTrash(file2, "slideRemoved_phash_");
                }

                if (success) {
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
                                         int hammingThreshold,
                                         bool useApplicationTrash,
                                         const QString& baseOutputDir)
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
                bool success = false;
                QString reason = QString("Excluded: %1 (distance: %2)").arg(entry.remark).arg(distance);

                if (useApplicationTrash) {
                    success = TrashManager::moveToApplicationTrash(filePath, baseOutputDir, "phash", reason);
                } else {
                    success = TrashManager::renameAndMoveToTrash(filePath, "slideRemoved_phash_");
                }

                if (success) {
                    movedFiles.append(filePath);
                    emit imageMovedToTrash(filePath, reason);
                }
                break;  // No need to check other exclusion entries
            }
        }
    }

    return movedFiles;
}

QStringList PostProcessor::classifyAndRemove(const QMap<QString, std::vector<uint8_t>>& imageHashes,
                                            const QString& mlModelPath,
                                            float mlNotSlideHighThreshold,
                                            float mlNotSlideLowThreshold,
                                            float mlMaybeSlideHighThreshold,
                                            float mlMaybeSlideLowThreshold,
                                            float mlSlideMaxThreshold,
                                            bool mlDeleteMaybeSlides,
                                            const QString& mlExecutionProvider,
                                            bool useApplicationTrash,
                                            const QString& baseOutputDir)
{
    QStringList movedFiles;

    if (!MLClassifier::isAvailable()) {
        qWarning() << "PostProcessor: ML classification requested but ONNX Runtime not available";
        return movedFiles;
    }

    if (mlModelPath.isEmpty()) {
        qWarning() << "PostProcessor: ML model path is empty";
        return movedFiles;
    }

    // Convert execution provider string to enum
    MLClassifier::ExecutionProvider provider = MLClassifier::stringToExecutionProvider(mlExecutionProvider);

    // Initialize ML classifier
    MLClassifier classifier(mlModelPath, provider);

    if (!classifier.isInitialized()) {
        QString errorMsg = classifier.getErrorMessage();
        qWarning() << "PostProcessor: Failed to initialize ML classifier:" << errorMsg;
        emit mlClassificationFailed(errorMsg);
        return movedFiles;
    }

    QString activeProvider = classifier.getActiveExecutionProvider();
    qInfo() << "PostProcessor: ML classification using" << activeProvider;

    // Emit signal with execution provider info
    emit mlClassificationStarted(activeProvider);

    // Get list of remaining images (after pHash post-processing)
    QStringList imagePaths = imageHashes.keys();

    if (imagePaths.isEmpty()) {
        return movedFiles;
    }

    // Classify all images
    QVector<ClassificationResult> results = classifier.classifyBatch(imagePaths);

    // Process results and remove unwanted images
    for (const ClassificationResult& result : results) {
        if (result.error) {
            qWarning() << "PostProcessor: Classification error for" << result.imagePath
                      << ":" << result.errorMessage;
            continue;
        }

        // Determine if image should be kept using 2-stage logic
        MLClassifier::CategoryThresholds notSlideThresholds(mlNotSlideHighThreshold, mlNotSlideLowThreshold);
        MLClassifier::CategoryThresholds maybeSlideThresholds(mlMaybeSlideHighThreshold, mlMaybeSlideLowThreshold);

        bool shouldKeep = MLClassifier::shouldKeepImage(result, notSlideThresholds,
                                                        maybeSlideThresholds, mlSlideMaxThreshold,
                                                        mlDeleteMaybeSlides);

        if (!shouldKeep) {
            // Move to trash
            bool success = false;
            QString reason = QString("ML: %1 (confidence: %2)")
                                .arg(result.predictedClass)
                                .arg(result.confidence, 0, 'f', 3);

            if (useApplicationTrash) {
                success = TrashManager::moveToApplicationTrash(result.imagePath, baseOutputDir, "ml", reason);
            } else {
                success = TrashManager::renameAndMoveToTrash(result.imagePath, "slideRemoved_ml_");
            }

            if (success) {
                movedFiles.append(result.imagePath);
                emit imageMovedToTrash(result.imagePath,
                    QString("ML: %1 (confidence: %2)")
                        .arg(result.predictedClass)
                        .arg(result.confidence, 0, 'f', 3));

                qInfo() << "PostProcessor: Removed" << QFileInfo(result.imagePath).fileName()
                       << "- classified as" << result.predictedClass
                       << "with confidence" << result.confidence;
            }
        }
    }

    qInfo() << "PostProcessor: ML classification complete -" << movedFiles.size()
           << "images removed out of" << imagePaths.size();

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
