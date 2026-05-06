#ifndef CROPMANAGER_H
#define CROPMANAGER_H

#include <QString>
#include <QRect>
#include "cropentry.h"

/**
 * @brief Static helpers for the `.extractorCrop/` non-destructive crop store.
 *
 * Mirrors TrashManager. Each cropped slide has its pre-crop original kept in
 * `<base>/.extractorCrop/slideOriginal_<video>_<index>.jpg`, indexed by
 * `metadata.json` (managed by CropMetadata). The live `slide_*.jpg` is the
 * cropped version. Restore brings the original back; recrop reuses the same
 * original (the backup file is never overwritten).
 */
class CropManager
{
public:
    /**
     * @brief Path to the `.extractorCrop/` directory.
     */
    static QString cropDirectory(const QString& baseOutputDir);

    /**
     * @brief Encode a backup filename for a live extracted slide path.
     *        e.g., "/.../slides_Lecture01/slide_Lecture01_001.jpg" → "slideOriginal_Lecture01_001.jpg"
     */
    static QString encodeBackupFilename(const QString& livePath);

    /**
     * @brief Decode `slideOriginal_<video>_<index>.jpg` back to its parts.
     */
    static bool decodeBackupFilename(const QString& backupFilename,
                                     QString& videoName,
                                     QString& slideIndex);

    /**
     * @brief Look up an existing crop entry for a live slide path. Returns true
     *        and fills `outEntry` if a match is found.
     */
    static bool findEntryForLivePath(const QString& livePath,
                                     const QString& baseOutputDir,
                                     CropEntry& outEntry);

    /**
     * @brief Apply a crop to the live slide, preserving the original in
     *        `.extractorCrop/` (or reusing the existing backup if recrop).
     *
     * @param livePath Full path to the live extracted slide (e.g. .../slides_X/slide_X_001.jpg).
     * @param baseOutputDir Base output directory.
     * @param cropRectImagePixels Crop rectangle in original-image pixels.
     * @param jpegQuality JPEG quality (1–100) used when re-saving the cropped live file.
     * @return true on success.
     */
    static bool applyCrop(const QString& livePath,
                          const QString& baseOutputDir,
                          const QRect& cropRectImagePixels,
                          int jpegQuality);

    /**
     * @brief Restore the original (uncropped) image for a backup filename.
     *        Moves `<cropDir>/<backupFilename>` back to its live location and
     *        removes the metadata entry.
     */
    static bool restoreCrop(const QString& backupFilename,
                            const QString& baseOutputDir);

    /**
     * @brief Move all `.extractorCrop/` entries whose `originalFolder` matches
     *        `folderName` to system trash (or delete) and remove their metadata
     *        entries. Mirrors TrashManager::emptyApplicationTrashForFolder.
     * @return Number of files successfully removed.
     */
    static int clearCropsForFolder(const QString& baseOutputDir,
                                   const QString& folderName,
                                   bool moveToSystemTrash);
};

#endif // CROPMANAGER_H
