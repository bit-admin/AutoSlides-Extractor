#include "cropmanager.h"
#include "cropmetadata.h"
#include "imageiohelper.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include <QDebug>
#include <opencv2/opencv.hpp>

QString CropManager::cropDirectory(const QString& baseOutputDir)
{
    return QDir(baseOutputDir).filePath(".extractorCrop");
}

QString CropManager::encodeBackupFilename(const QString& livePath)
{
    QFileInfo fileInfo(livePath);
    QString filename = fileInfo.fileName();

    // Expected: slide_VideoName_001.jpg
    QRegularExpression re("^slide_(.+)_(\\d{3})\\.jpg$");
    QRegularExpressionMatch match = re.match(filename);

    if (!match.hasMatch()) {
        qWarning() << "CropManager: Cannot encode backup filename, unexpected format:" << filename;
        return QString("slideOriginal_%1").arg(filename.mid(6)); // Strip "slide_"
    }

    QString videoName = match.captured(1);
    QString slideIndex = match.captured(2);
    return QString("slideOriginal_%1_%2.jpg").arg(videoName).arg(slideIndex);
}

bool CropManager::decodeBackupFilename(const QString& backupFilename,
                                       QString& videoName,
                                       QString& slideIndex)
{
    QRegularExpression re("^slideOriginal_(.+)_(\\d{3})\\.jpg$");
    QRegularExpressionMatch match = re.match(backupFilename);

    if (!match.hasMatch()) {
        qWarning() << "CropManager: Cannot decode backup filename:" << backupFilename;
        return false;
    }

    videoName = match.captured(1);
    slideIndex = match.captured(2);
    return true;
}

bool CropManager::findEntryForLivePath(const QString& livePath,
                                       const QString& baseOutputDir,
                                       CropEntry& outEntry)
{
    QFileInfo info(livePath);
    QString liveFilename = info.fileName();
    QString liveFolder = info.dir().dirName();

    QRegularExpression re("^slide_(.+)_(\\d{3})\\.jpg$");
    QRegularExpressionMatch match = re.match(liveFilename);
    if (!match.hasMatch()) {
        return false;
    }
    QString videoName = match.captured(1);
    QString slideIndex = match.captured(2);

    QList<CropEntry> entries = CropMetadata::getEntries(cropDirectory(baseOutputDir));
    for (const CropEntry& e : entries) {
        if (e.videoName == videoName && e.slideIndex == slideIndex
            && (e.originalFolder == liveFolder || e.originalFolder.isEmpty())) {
            outEntry = e;
            return true;
        }
    }
    return false;
}

bool CropManager::applyCrop(const QString& livePath,
                            const QString& baseOutputDir,
                            const QRect& cropRectImagePixels,
                            int jpegQuality)
{
    if (!QFile::exists(livePath)) {
        qWarning() << "CropManager: Live file does not exist:" << livePath;
        return false;
    }

    if (!cropRectImagePixels.isValid() || cropRectImagePixels.width() <= 0
        || cropRectImagePixels.height() <= 0) {
        qWarning() << "CropManager: Invalid crop rect:" << cropRectImagePixels;
        return false;
    }

    QString cropDir = cropDirectory(baseOutputDir);
    QDir dir(cropDir);
    if (!dir.exists() && !dir.mkpath(".")) {
        qWarning() << "CropManager: Failed to create crop directory:" << cropDir;
        return false;
    }

    // Find or create the backup of the original image.
    CropEntry existingEntry;
    bool isRecrop = findEntryForLivePath(livePath, baseOutputDir, existingEntry);

    QString backupFilename;
    QString backupPath;

    if (isRecrop) {
        backupFilename = existingEntry.backupFilename;
        backupPath = dir.filePath(backupFilename);
        if (!QFile::exists(backupPath)) {
            qWarning() << "CropManager: Recrop expected but backup is missing:" << backupPath;
            // Fall through and treat this as a fresh crop using the live as the original.
            isRecrop = false;
        }
    }

    if (!isRecrop) {
        backupFilename = encodeBackupFilename(livePath);
        backupPath = dir.filePath(backupFilename);

        // Copy the current live image as the original backup.
        if (QFile::exists(backupPath)) {
            // Stale file from prior aborted crop — overwrite.
            QFile::remove(backupPath);
        }
        if (!QFile::copy(livePath, backupPath)) {
            qWarning() << "CropManager: Failed to back up original to:" << backupPath;
            return false;
        }
    }

    // Load the original (always the backup, never the cropped live).
    cv::Mat original = ImageIOHelper::imreadUnicode(backupPath, cv::IMREAD_COLOR);
    if (original.empty()) {
        qWarning() << "CropManager: Failed to read backup image:" << backupPath;
        return false;
    }

    // Clamp crop rect to image bounds.
    QRect rect = cropRectImagePixels;
    rect.setX(std::max(0, rect.x()));
    rect.setY(std::max(0, rect.y()));
    int maxW = original.cols - rect.x();
    int maxH = original.rows - rect.y();
    rect.setWidth(std::min(rect.width(), maxW));
    rect.setHeight(std::min(rect.height(), maxH));

    if (rect.width() <= 0 || rect.height() <= 0) {
        qWarning() << "CropManager: Crop rect collapsed after clamping:" << rect;
        return false;
    }

    cv::Rect cvRect(rect.x(), rect.y(), rect.width(), rect.height());
    cv::Mat cropped = original(cvRect).clone();

    std::vector<int> params;
    params.push_back(cv::IMWRITE_JPEG_QUALITY);
    params.push_back(std::clamp(jpegQuality, 1, 100));

    if (!ImageIOHelper::imwriteUnicode(livePath, cropped, params)) {
        qWarning() << "CropManager: Failed to write cropped image to:" << livePath;
        return false;
    }

    // Update metadata.
    QFileInfo info(livePath);
    QString liveFolder = info.dir().dirName();
    QString videoName, slideIndex;
    if (!decodeBackupFilename(backupFilename, videoName, slideIndex)) {
        // Fallback parse from live filename.
        QRegularExpression re("^slide_(.+)_(\\d{3})\\.jpg$");
        QRegularExpressionMatch m = re.match(info.fileName());
        if (m.hasMatch()) {
            videoName = m.captured(1);
            slideIndex = m.captured(2);
        }
    }

    CropEntry entry(backupFilename, liveFolder, videoName, slideIndex, rect,
                    QDateTime::currentDateTime());

    if (!CropMetadata::updateEntry(cropDirectory(baseOutputDir), entry)) {
        qWarning() << "CropManager: Failed to update metadata for:" << backupFilename;
        // The crop did succeed on disk; metadata failure is a warning.
    }

    qInfo() << "CropManager: Applied crop:" << livePath << "rect:" << rect
            << (isRecrop ? "(recrop)" : "(initial)");
    return true;
}

bool CropManager::restoreCrop(const QString& backupFilename,
                              const QString& baseOutputDir)
{
    QString cropDir = cropDirectory(baseOutputDir);
    QString backupPath = QDir(cropDir).filePath(backupFilename);

    if (!QFile::exists(backupPath)) {
        qWarning() << "CropManager: Backup file does not exist:" << backupPath;
        // Still try to scrub a stale metadata entry.
        CropMetadata::removeEntry(cropDir, backupFilename);
        return false;
    }

    // Look up the entry to find the live path.
    QList<CropEntry> entries = CropMetadata::getEntries(cropDir);
    CropEntry entry;
    bool found = false;
    for (const CropEntry& e : entries) {
        if (e.backupFilename == backupFilename) {
            entry = e;
            found = true;
            break;
        }
    }

    if (!found) {
        qWarning() << "CropManager: No metadata entry for backup:" << backupFilename;
        return false;
    }

    QString livePath = entry.getLivePath(baseOutputDir);

    QString liveFolderPath = QDir(baseOutputDir).filePath(entry.originalFolder);
    if (!QDir(liveFolderPath).exists()) {
        qWarning() << "CropManager: Original folder does not exist:" << liveFolderPath;
        return false;
    }

    // Replace the cropped live file with the original.
    if (QFile::exists(livePath)) {
        if (!QFile::remove(livePath)) {
            qWarning() << "CropManager: Failed to remove cropped live file:" << livePath;
            return false;
        }
    }

    if (!QFile::rename(backupPath, livePath)) {
        qWarning() << "CropManager: Failed to restore original to:" << livePath;
        return false;
    }

    if (!CropMetadata::removeEntry(cropDir, backupFilename)) {
        qWarning() << "CropManager: Failed to remove metadata entry for:" << backupFilename;
    }

    qInfo() << "CropManager: Restored original:" << backupFilename << "->" << livePath;
    return true;
}

int CropManager::clearCropsForFolder(const QString& baseOutputDir,
                                     const QString& folderName,
                                     bool moveToSystemTrash)
{
    QString cropDir = cropDirectory(baseOutputDir);
    QList<CropEntry> entries = CropMetadata::getEntries(cropDir);

    int successCount = 0;

    for (const CropEntry& entry : entries) {
        const QString matchA = entry.originalFolder;
        const QString matchB = QString("slides_%1").arg(entry.videoName);
        if (matchA != folderName && matchB != folderName) {
            continue;
        }

        QString backupPath = entry.getBackupPath(baseOutputDir);

        if (QFile::exists(backupPath)) {
            bool ok = moveToSystemTrash ? QFile::moveToTrash(backupPath)
                                         : QFile::remove(backupPath);
            if (ok) {
                successCount++;
            } else {
                qWarning() << "CropManager: Failed to remove backup file:" << backupPath;
                continue;
            }
        }

        CropMetadata::removeEntry(cropDir, entry.backupFilename);
    }

    qInfo() << "CropManager: Cleared crops for folder" << folderName << ":" << successCount << "files";
    return successCount;
}
