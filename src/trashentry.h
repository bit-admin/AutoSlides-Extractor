#ifndef TRASHENTRY_H
#define TRASHENTRY_H

#include <QString>
#include <QDateTime>
#include <QPixmap>
#include <QDir>
#include <QFileInfo>

/**
 * @brief Structure representing a trashed slide image
 *
 * Stores metadata about a slide that has been moved to application trash,
 * including its original location, removal reason, and timestamp.
 */
struct TrashEntry {
    QString trashedFilename;      // e.g., "slideRemoved_phash_Lecture01_001.jpg"
    QString originalFolder;       // e.g., "slides_Lecture01"
    QString videoName;            // e.g., "Lecture01"
    QString slideIndex;           // e.g., "001"
    QString method;               // "phash" or "ml"
    QString reason;               // e.g., "Duplicate (distance: 5)"
    QDateTime timestamp;          // When the file was trashed

    /**
     * @brief Default constructor
     */
    TrashEntry()
        : timestamp(QDateTime::currentDateTime()) {}

    /**
     * @brief Constructor with all fields
     */
    TrashEntry(const QString& trashedFilename,
               const QString& originalFolder,
               const QString& videoName,
               const QString& slideIndex,
               const QString& method,
               const QString& reason,
               const QDateTime& timestamp = QDateTime::currentDateTime())
        : trashedFilename(trashedFilename),
          originalFolder(originalFolder),
          videoName(videoName),
          slideIndex(slideIndex),
          method(method),
          reason(reason),
          timestamp(timestamp) {}

    /**
     * @brief Get the original file path before it was trashed
     * @param baseOutputDir Base output directory (e.g., ~/Downloads/SlidesExtractor)
     * @return Full path to original location (e.g., .../slides_Lecture01/slide_Lecture01_001.jpg)
     */
    QString getOriginalPath(const QString& baseOutputDir) const {
        QString originalFilename = QString("slide_%1_%2.jpg").arg(videoName).arg(slideIndex);
        return QDir(baseOutputDir).filePath(originalFolder + "/" + originalFilename);
    }

    /**
     * @brief Get the current trashed file path
     * @param baseOutputDir Base output directory
     * @return Full path to trashed file (e.g., .../.extractorTrash/slideRemoved_phash_Lecture01_001.jpg)
     */
    QString getTrashedPath(const QString& baseOutputDir) const {
        return QDir(baseOutputDir).filePath(".extractorTrash/" + trashedFilename);
    }

    /**
     * @brief Get thumbnail of the trashed image
     * @param baseOutputDir Base output directory
     * @param size Thumbnail size (default: 256x256)
     * @return QPixmap thumbnail, or null pixmap if loading fails
     */
    QPixmap getThumbnail(const QString& baseOutputDir, int size = 256) const {
        QString path = getTrashedPath(baseOutputDir);
        QPixmap pixmap(path);
        if (pixmap.isNull()) {
            return QPixmap();
        }
        return pixmap.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    /**
     * @brief Check if the original folder still exists
     * @param baseOutputDir Base output directory
     * @return true if the original folder exists
     */
    bool originalFolderExists(const QString& baseOutputDir) const {
        QString folderPath = QDir(baseOutputDir).filePath(originalFolder);
        return QDir(folderPath).exists();
    }

    /**
     * @brief Get display name for UI
     * @return User-friendly display name (e.g., "Lecture01 #001")
     */
    QString getDisplayName() const {
        return QString("%1 #%2").arg(videoName).arg(slideIndex);
    }

    /**
     * @brief Get method display name
     * @return User-friendly method name ("pHash", "ML", or "Manual")
     */
    QString getMethodDisplayName() const {
        if (method == "phash") {
            return "pHash";
        } else if (method == "ml") {
            return "ML";
        } else if (method == "manual") {
            return "Manual";
        }
        return method;
    }
};

#endif // TRASHENTRY_H
