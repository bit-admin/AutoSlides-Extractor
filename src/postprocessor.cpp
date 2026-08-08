#include "postprocessor.h"
#include "trashmanager.h"
#include "timelinemetadata.h"
#include "mlclassifier.h"
#include "autocropdetector.h"
#include "cropmanager.h"
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QDebug>
#include <memory>

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
                                                    const QString& baseOutputDir,
                                                    bool mlAutoCropMaybeSlides,
                                                    bool mlPostCropDedup,
                                                    const AutoCropConfig& autoCropConfig,
                                                    int jpegQuality)
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

    QStringList autoCroppedKept;

    // ML classification if enabled
    if (enableMLClassification && MLClassifier::isAvailable()) {
        QStringList mlRemoved = classifyAndRemove(imageHashes, mlModelPath,
                                                  mlNotSlideHighThreshold,
                                                  mlNotSlideLowThreshold,
                                                  mlMaybeSlideHighThreshold,
                                                  mlMaybeSlideLowThreshold,
                                                  mlSlideMaxThreshold,
                                                  mlDeleteMaybeSlides,
                                                  mlExecutionProvider,
                                                  useApplicationTrash,
                                                  baseOutputDir,
                                                  mlAutoCropMaybeSlides,
                                                  autoCropConfig,
                                                  jpegQuality,
                                                  &autoCroppedKept);
        m_movedToTrash.append(mlRemoved);
        result.removedByML = mlRemoved.size();
        result.autoCroppedKept = autoCroppedKept.size();

        // Drop trashed paths from the hash map (auto-cropped kept stay — hashes are
        // stale for those; post-crop dedup rehashes candidates from disk).
        for (const QString& file : mlRemoved) {
            imageHashes.remove(file);
        }
    }

    // Candidate-only pHash after successful auto-crops (post-crop pixels).
    if (mlPostCropDedup && !autoCroppedKept.isEmpty()) {
        QStringList postCropDupes = removePostCropDuplicates(
            imageDir, autoCroppedKept, imageHashes, hammingThreshold,
            useApplicationTrash, baseOutputDir);
        m_movedToTrash.append(postCropDupes);
        result.removedByPHash += postCropDupes.size();
        result.removedByPostCropPHash = postCropDupes.size();
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
                                                                   "phash_duplicate",
                                                                   QString("Duplicate (distance: %1)").arg(distance));
                } else {
                    success = TrashManager::renameAndMoveToTrash(file2, "slideRemoved_phash_");
                }

                if (success) {
                    movedFiles.append(file2);
                    emit imageMovedToTrash(file2, QString("Duplicate (distance: %1)").arg(distance));
                    // Keep event; re-link later span to first-kept basename
                    TimelineMetadata::markDuplicate(
                        QFileInfo(file2).absolutePath(),
                        QFileInfo(file2).fileName(),
                        QFileInfo(file1).fileName());
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
                    success = TrashManager::moveToApplicationTrash(filePath, baseOutputDir, "phash",
                                                                   "phash_excluded", reason);
                } else {
                    success = TrashManager::renameAndMoveToTrash(filePath, "slideRemoved_phash_");
                }

                if (success) {
                    movedFiles.append(filePath);
                    emit imageMovedToTrash(filePath, reason);
                    TimelineMetadata::markGap(
                        QFileInfo(filePath).absolutePath(),
                        QFileInfo(filePath).fileName(),
                        QStringLiteral("exclusion"));
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
                                            const QString& baseOutputDir,
                                            bool mlAutoCropMaybeSlides,
                                            const AutoCropConfig& autoCropConfig,
                                            int jpegQuality,
                                            QStringList* outAutoCroppedKept)
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

    // Lazy detector: only built if we actually try auto-crop on a may_be_slide.
    std::unique_ptr<AutoCropDetector> autoCropDetector;
    const bool canAutoCrop = mlAutoCropMaybeSlides
                             && mlDeleteMaybeSlides
                             && !baseOutputDir.isEmpty()
                             && QDir(baseOutputDir).exists();

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

        if (shouldKeep) {
            continue;
        }

        const bool isMaybeSlide = result.predictedClass.startsWith(QLatin1String("may_be_slide"));

        // Try in-place auto-crop for may_be_slide before trashing (Review Auto Crop spirit).
        if (isMaybeSlide && canAutoCrop) {
            if (!autoCropDetector) {
                autoCropDetector = std::make_unique<AutoCropDetector>(autoCropConfig);
            }
            AutoCropResult ac = autoCropDetector->detect(result.imagePath);
            if (ac.isValid()
                && CropManager::applyCrop(result.imagePath, baseOutputDir, ac.bbox,
                                          jpegQuality, /*autoCropped=*/true)) {
                if (outAutoCroppedKept) {
                    outAutoCroppedKept->append(result.imagePath);
                }
                qInfo() << "PostProcessor: Auto-cropped and kept"
                        << QFileInfo(result.imagePath).fileName()
                        << "- classified as" << result.predictedClass
                        << "with confidence" << result.confidence
                        << "bbox" << ac.bbox;
                continue; // keep live file
            }
            qInfo() << "PostProcessor: Auto-crop failed for"
                    << QFileInfo(result.imagePath).fileName()
                    << "- falling back to trash"
                    << (ac.errorMessage.isEmpty() ? QString() : ac.errorMessage);
        }

        // Move to trash
        bool success = false;
        QString reason = QString("ML: %1 (confidence: %2)")
                            .arg(result.predictedClass)
                            .arg(result.confidence, 0, 'f', 3);

        QString category;
        if (result.predictedClass.startsWith("not_slide")) {
            category = "ml_not_slide";
        } else if (isMaybeSlide) {
            category = "ml_maybe_slide";
        }

        if (useApplicationTrash) {
            success = TrashManager::moveToApplicationTrash(result.imagePath, baseOutputDir, "ml",
                                                           category, reason);
        } else {
            success = TrashManager::renameAndMoveToTrash(result.imagePath, "slideRemoved_ml_");
        }

        if (success) {
            movedFiles.append(result.imagePath);
            emit imageMovedToTrash(result.imagePath,
                QString("ML: %1 (confidence: %2)")
                    .arg(result.predictedClass)
                    .arg(result.confidence, 0, 'f', 3));
            TimelineMetadata::markGap(
                QFileInfo(result.imagePath).absolutePath(),
                QFileInfo(result.imagePath).fileName(),
                QStringLiteral("ai_filtered"));

            qInfo() << "PostProcessor: Removed" << QFileInfo(result.imagePath).fileName()
                   << "- classified as" << result.predictedClass
                   << "with confidence" << result.confidence;
        }
    }

    qInfo() << "PostProcessor: ML classification complete -" << movedFiles.size()
           << "images removed out of" << imagePaths.size()
           << ";" << (outAutoCroppedKept ? outAutoCroppedKept->size() : 0)
           << "auto-cropped and kept";

    return movedFiles;
}

QStringList PostProcessor::removePostCropDuplicates(const QString& imageDir,
                                                    const QStringList& autoCroppedKept,
                                                    const QMap<QString, std::vector<uint8_t>>& priorHashes,
                                                    int hammingThreshold,
                                                    bool useApplicationTrash,
                                                    const QString& baseOutputDir)
{
    QStringList movedFiles;

    if (autoCroppedKept.isEmpty()) {
        return movedFiles;
    }

    QDir dir(imageDir);
    QStringList filters;
    filters << "*.jpg" << "*.jpeg" << "*.png" << "*.bmp";
    QStringList remaining = dir.entryList(filters, QDir::Files, QDir::Name);
    for (QString& f : remaining) {
        f = dir.absoluteFilePath(f);
    }

    QSet<QString> candidateSet(autoCroppedKept.begin(), autoCroppedKept.end());

    // Candidates still on disk, stable name order (entryList already Name-sorted).
    QStringList candidates;
    QStringList otherActive;
    for (const QString& path : remaining) {
        if (candidateSet.contains(path)) {
            candidates.append(path);
        } else {
            otherActive.append(path);
        }
    }

    if (candidates.isEmpty()) {
        return movedFiles;
    }

    // Seed seen hashes from non-candidate remaining slides. Prefer stage-1 hashes
    // (uncropped pixels still match for those files).
    struct SeenEntry {
        QString path;
        std::vector<uint8_t> hash;
    };
    QList<SeenEntry> seen;
    seen.reserve(otherActive.size() + candidates.size());

    for (const QString& path : otherActive) {
        std::vector<uint8_t> hash;
        if (priorHashes.contains(path) && !priorHashes.value(path).empty()) {
            hash = priorHashes.value(path);
        } else {
            hash = PHashCalculator::calculatePHash(path);
        }
        if (!hash.empty()) {
            seen.append({path, std::move(hash)});
        }
    }

    for (const QString& candPath : candidates) {
        // Must rehash: live pixels were rewritten by applyCrop.
        std::vector<uint8_t> candHash = PHashCalculator::calculatePHash(candPath);
        if (candHash.empty()) {
            qWarning() << "PostProcessor: post-crop pHash failed for" << candPath;
            continue;
        }

        bool isDup = false;
        QString matchPath;
        int matchDistance = -1;
        for (const SeenEntry& s : seen) {
            int distance = PHashCalculator::hammingDistance(candHash, s.hash);
            if (distance >= 0 && distance <= hammingThreshold) {
                isDup = true;
                matchPath = s.path;
                matchDistance = distance;
                break;
            }
        }

        if (isDup) {
            const QString reason = QString("Duplicate of %1 (distance: %2)")
                                       .arg(QFileInfo(matchPath).fileName())
                                       .arg(matchDistance);
            bool success = false;
            if (useApplicationTrash) {
                // Leave .extractorCrop backup/metadata intact for Review restore.
                success = TrashManager::moveToApplicationTrash(
                    candPath, baseOutputDir, "phash", "phash_duplicate", reason);
            } else {
                success = TrashManager::renameAndMoveToTrash(candPath, "slideRemoved_phash_");
            }
            if (success) {
                movedFiles.append(candPath);
                emit imageMovedToTrash(candPath, reason);
                TimelineMetadata::markDuplicate(
                    imageDir,
                    QFileInfo(candPath).fileName(),
                    QFileInfo(matchPath).fileName());
                qInfo() << "PostProcessor: Post-crop duplicate removed"
                        << QFileInfo(candPath).fileName() << reason;
            }
        } else {
            // Earlier kept crop wins for later candidates.
            seen.append({candPath, std::move(candHash)});
        }
    }

    qInfo() << "PostProcessor: Post-crop pHash complete -" << movedFiles.size()
           << "duplicates removed from" << candidates.size() << "auto-cropped candidates";

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
