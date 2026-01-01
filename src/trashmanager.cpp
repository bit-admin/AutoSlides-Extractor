#include "trashmanager.h"
#include "trashmetadata.h"
#include "trashentry.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include <QDebug>

bool TrashManager::moveToTrash(const QString& filePath)
{
    if (!QFile::exists(filePath)) {
        return false;
    }

    // Use Qt's built-in moveToTrash function (available since Qt 5.15)
    return QFile::moveToTrash(filePath);
}

bool TrashManager::renameAndMoveToTrash(const QString& filePath, const QString& newPrefix)
{
    if (!QFile::exists(filePath)) {
        return false;
    }

    QFileInfo fileInfo(filePath);
    QString fileName = fileInfo.fileName();
    QString dirPath = fileInfo.absolutePath();

    // Check if filename starts with "slide_"
    if (!fileName.startsWith("slide_")) {
        // If it doesn't start with "slide_", just move to trash without renaming
        return moveToTrash(filePath);
    }

    // Replace "slide_" with the new prefix
    QString newFileName = fileName;
    newFileName.replace(0, 6, newPrefix);  // "slide_" is 6 characters

    // Construct new file path
    QString newFilePath = QDir(dirPath).filePath(newFileName);

    // Rename the file
    if (!QFile::rename(filePath, newFilePath)) {
        // If rename fails, try to move original file to trash
        return moveToTrash(filePath);
    }

    // Move renamed file to trash
    return QFile::moveToTrash(newFilePath);
}

int TrashManager::moveToTrash(const QStringList& filePaths)
{
    int successCount = 0;
    for (const QString& filePath : filePaths) {
        if (moveToTrash(filePath)) {
            successCount++;
        }
    }
    return successCount;
}

bool TrashManager::isTrashAvailable()
{
    // QFile::moveToTrash is available on all major platforms (macOS, Windows, Linux)
    // since Qt 5.15
    return true;
}

// ========== Application Trash Methods ==========

QString TrashManager::getTrashDirectory(const QString& baseOutputDir)
{
    return QDir(baseOutputDir).filePath(".extractorTrash");
}

QString TrashManager::encodeTrashFilename(const QString& originalPath, const QString& method)
{
    QFileInfo fileInfo(originalPath);
    QString filename = fileInfo.fileName();

    // Check if already encoded (starts with slideRemoved_)
    if (filename.startsWith("slideRemoved_")) {
        qWarning() << "TrashManager: File already encoded, skipping:" << filename;
        return filename;  // Return as-is
    }

    // Expected format: slide_VideoName_001.jpg
    // Parse to extract videoName and index
    QRegularExpression re("^slide_(.+)_(\\d{3})\\.jpg$");
    QRegularExpressionMatch match = re.match(filename);

    if (!match.hasMatch()) {
        qWarning() << "TrashManager: Cannot encode filename, unexpected format:" << filename;
        // Fallback: just prepend slideRemoved_{method}_
        return QString("slideRemoved_%1_%2").arg(method).arg(filename.mid(6)); // Remove "slide_"
    }

    QString videoName = match.captured(1);
    QString slideIndex = match.captured(2);

    // Encode as: slideRemoved_{method}_{videoName}_{index}.jpg
    return QString("slideRemoved_%1_%2_%3.jpg").arg(method).arg(videoName).arg(slideIndex);
}

bool TrashManager::decodeTrashFilename(const QString& trashedFilename,
                                      QString& videoName,
                                      QString& slideIndex,
                                      QString& method)
{
    // Expected format: slideRemoved_{method}_{videoName}_{index}.jpg
    QRegularExpression re("^slideRemoved_(phash|ml|manual)_(.+)_(\\d{3})\\.jpg$");
    QRegularExpressionMatch match = re.match(trashedFilename);

    if (!match.hasMatch()) {
        qWarning() << "TrashManager: Cannot decode filename:" << trashedFilename;
        return false;
    }

    method = match.captured(1);
    videoName = match.captured(2);
    slideIndex = match.captured(3);

    return true;
}

bool TrashManager::moveToApplicationTrash(const QString& filePath,
                                         const QString& baseOutputDir,
                                         const QString& method,
                                         const QString& reason)
{
    if (!QFile::exists(filePath)) {
        qWarning() << "TrashManager: File does not exist:" << filePath;
        return false;
    }

    // Get trash directory
    QString trashDir = getTrashDirectory(baseOutputDir);
    QDir dir(trashDir);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            qWarning() << "TrashManager: Failed to create trash directory:" << trashDir;
            return false;
        }
    }

    // Encode filename
    QString encodedFilename = encodeTrashFilename(filePath, method);
    QString trashedPath = dir.filePath(encodedFilename);

    // Move file to trash
    if (!QFile::rename(filePath, trashedPath)) {
        qWarning() << "TrashManager: Failed to move file to trash:" << filePath << "->" << trashedPath;
        return false;
    }

    // Extract metadata from original path
    QFileInfo originalFileInfo(filePath);
    QString originalFolder = originalFileInfo.dir().dirName(); // e.g., "slides_Lecture01"

    // Decode to get videoName and slideIndex
    QString videoName, slideIndex, decodedMethod;
    if (!decodeTrashFilename(encodedFilename, videoName, slideIndex, decodedMethod)) {
        qWarning() << "TrashManager: Failed to decode filename for metadata:" << encodedFilename;
        // Continue anyway, file is already moved
    }

    // Create metadata entry
    TrashEntry entry(encodedFilename, originalFolder, videoName, slideIndex, method, reason);

    // Add to metadata
    if (!TrashMetadata::addEntry(trashDir, entry)) {
        qWarning() << "TrashManager: Failed to add metadata entry for:" << encodedFilename;
        // File is still in trash, just metadata failed
    }

    qInfo() << "TrashManager: Moved to application trash:" << filePath << "->" << encodedFilename;
    return true;
}

bool TrashManager::restoreFromApplicationTrash(const QString& trashedFilename,
                                              const QString& baseOutputDir)
{
    QString trashDir = getTrashDirectory(baseOutputDir);
    QString trashedPath = QDir(trashDir).filePath(trashedFilename);

    if (!QFile::exists(trashedPath)) {
        qWarning() << "TrashManager: Trashed file does not exist:" << trashedPath;
        return false;
    }

    // Decode filename to get metadata
    QString videoName, slideIndex, method;
    if (!decodeTrashFilename(trashedFilename, videoName, slideIndex, method)) {
        qWarning() << "TrashManager: Cannot restore, failed to decode filename:" << trashedFilename;
        // Try to move to system trash as fallback
        if (moveToTrash(trashedPath)) {
            qInfo() << "TrashManager: Moved undecodable file to system trash:" << trashedFilename;
        }
        return false;
    }

    // Construct original path
    QString originalFolder = QString("slides_%1").arg(videoName);
    QString originalFilename = QString("slide_%1_%2.jpg").arg(videoName).arg(slideIndex);
    QString originalPath = QDir(baseOutputDir).filePath(originalFolder + "/" + originalFilename);

    // Check if original folder exists
    QString originalFolderPath = QDir(baseOutputDir).filePath(originalFolder);
    if (!QDir(originalFolderPath).exists()) {
        qWarning() << "TrashManager: Original folder does not exist:" << originalFolderPath;
        qWarning() << "TrashManager: Moving to system trash instead:" << trashedFilename;

        // Move to system trash as fallback
        if (moveToTrash(trashedPath)) {
            // Remove from metadata
            TrashMetadata::removeEntry(trashDir, trashedFilename);
            return false;
        }
        return false;
    }

    // Restore file
    if (!QFile::rename(trashedPath, originalPath)) {
        qWarning() << "TrashManager: Failed to restore file:" << trashedPath << "->" << originalPath;
        return false;
    }

    // Remove from metadata
    if (!TrashMetadata::removeEntry(trashDir, trashedFilename)) {
        qWarning() << "TrashManager: Failed to remove metadata entry for:" << trashedFilename;
        // File is still restored, just metadata cleanup failed
    }

    qInfo() << "TrashManager: Restored from trash:" << trashedFilename << "->" << originalPath;
    return true;
}

int TrashManager::emptyApplicationTrash(const QString& baseOutputDir, bool moveToSystemTrash)
{
    QString trashDir = getTrashDirectory(baseOutputDir);

    // Get all entries from metadata
    QList<TrashEntry> entries = TrashMetadata::getEntries(trashDir);

    int successCount = 0;

    for (const TrashEntry& entry : entries) {
        QString trashedPath = entry.getTrashedPath(baseOutputDir);

        if (!QFile::exists(trashedPath)) {
            qWarning() << "TrashManager: Trashed file does not exist:" << trashedPath;
            continue;
        }

        if (moveToSystemTrash) {
            // Move to system trash
            if (moveToTrash(trashedPath)) {
                successCount++;
            }
        } else {
            // Delete permanently
            if (QFile::remove(trashedPath)) {
                successCount++;
            } else {
                qWarning() << "TrashManager: Failed to delete file:" << trashedPath;
            }
        }
    }

    // Remove metadata file
    QString metadataPath = TrashMetadata::getMetadataPath(trashDir);
    if (QFile::exists(metadataPath)) {
        QFile::remove(metadataPath);
    }

    qInfo() << "TrashManager: Emptied application trash:" << successCount << "files";
    return successCount;
}

int TrashManager::getTrashCount(const QString& baseOutputDir)
{
    QString trashDir = getTrashDirectory(baseOutputDir);
    QList<TrashEntry> entries = TrashMetadata::getEntries(trashDir);
    return entries.size();
}

int TrashManager::cleanupOldEntries(const QString& baseOutputDir, int retentionDays)
{
    if (retentionDays <= 0) {
        return 0;
    }

    QString trashDir = getTrashDirectory(baseOutputDir);
    QList<TrashEntry> entries = TrashMetadata::getEntries(trashDir);

    QDateTime cutoffDate = QDateTime::currentDateTime().addDays(-retentionDays);
    int removedCount = 0;

    QList<TrashEntry> remainingEntries;

    for (const TrashEntry& entry : entries) {
        if (entry.timestamp < cutoffDate) {
            // Remove old entry
            QString trashedPath = entry.getTrashedPath(baseOutputDir);
            if (QFile::exists(trashedPath)) {
                if (QFile::remove(trashedPath)) {
                    removedCount++;
                    qInfo() << "TrashManager: Removed old trash entry:" << entry.trashedFilename;
                }
            }
        } else {
            // Keep entry
            remainingEntries.append(entry);
        }
    }

    // Save updated metadata
    if (removedCount > 0) {
        TrashMetadata::save(trashDir, remainingEntries);
    }

    return removedCount;
}
