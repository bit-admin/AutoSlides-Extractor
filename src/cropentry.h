#ifndef CROPENTRY_H
#define CROPENTRY_H

#include <QString>
#include <QDateTime>
#include <QDir>
#include <QRect>

/**
 * @brief Structure representing the original (pre-crop) backup of a slide image.
 *
 * Stores metadata about an extracted slide that has been cropped, including the
 * backup filename, original folder, and the crop rectangle in original-image
 * pixel coordinates. The backup file lives in `<base>/.extractorCrop/`.
 */
struct CropEntry {
    QString backupFilename;   // e.g., "slideOriginal_Lecture01_001.jpg"
    QString originalFolder;   // e.g., "slides_Lecture01"
    QString videoName;        // e.g., "Lecture01"
    QString slideIndex;       // e.g., "001"
    int cropX = 0;            // crop rect (in original-image pixels)
    int cropY = 0;
    int cropW = 0;
    int cropH = 0;
    QDateTime timestamp;      // When the crop was applied / last updated

    CropEntry()
        : timestamp(QDateTime::currentDateTime()) {}

    CropEntry(const QString& backupFilename,
              const QString& originalFolder,
              const QString& videoName,
              const QString& slideIndex,
              const QRect& cropRect,
              const QDateTime& timestamp = QDateTime::currentDateTime())
        : backupFilename(backupFilename),
          originalFolder(originalFolder),
          videoName(videoName),
          slideIndex(slideIndex),
          cropX(cropRect.x()),
          cropY(cropRect.y()),
          cropW(cropRect.width()),
          cropH(cropRect.height()),
          timestamp(timestamp) {}

    QRect cropRect() const { return QRect(cropX, cropY, cropW, cropH); }

    /**
     * @brief Live (currently-on-disk) path of the cropped slide.
     */
    QString getLivePath(const QString& baseOutputDir) const {
        QString liveFilename = QString("slide_%1_%2.jpg").arg(videoName).arg(slideIndex);
        return QDir(baseOutputDir).filePath(originalFolder + "/" + liveFilename);
    }

    /**
     * @brief Path of the pre-crop original inside `.extractorCrop/`.
     */
    QString getBackupPath(const QString& baseOutputDir) const {
        return QDir(baseOutputDir).filePath(".extractorCrop/" + backupFilename);
    }

    QString getDisplayName() const {
        return QString("%1 #%2").arg(videoName).arg(slideIndex);
    }
};

#endif // CROPENTRY_H
