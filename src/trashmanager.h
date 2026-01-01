#ifndef TRASHMANAGER_H
#define TRASHMANAGER_H

#include <QString>
#include <QStringList>

/**
 * @brief Cross-platform trash/recycle bin manager
 *
 * Provides functionality to move files to the system trash/recycle bin
 * instead of permanently deleting them. Uses Qt's built-in QFile::moveToTrash()
 * which supports macOS, Windows, and Linux.
 */
class TrashManager
{
public:
    /**
     * @brief Move a file to trash/recycle bin
     * @param filePath Path to the file to move to trash
     * @return true if successful, false otherwise
     */
    static bool moveToTrash(const QString& filePath);

    /**
     * @brief Rename a file and then move it to trash/recycle bin
     * @param filePath Path to the file to move to trash
     * @param newPrefix New prefix to replace "slide_" with (e.g., "slideRemoved_phash_" or "slideRemoved_ml_")
     * @return true if successful, false otherwise
     */
    static bool renameAndMoveToTrash(const QString& filePath, const QString& newPrefix);

    /**
     * @brief Move multiple files to trash/recycle bin
     * @param filePaths List of file paths to move to trash
     * @return Number of files successfully moved to trash
     */
    static int moveToTrash(const QStringList& filePaths);

    /**
     * @brief Check if trash functionality is available on this platform
     * @return true if trash functionality is available
     */
    static bool isTrashAvailable();

    // ========== Application Trash Methods ==========

    /**
     * @brief Move a file to application trash (.extractorTrash/)
     * @param filePath Path to the file to move
     * @param baseOutputDir Base output directory (e.g., ~/Downloads/SlidesExtractor)
     * @param method Removal method ("phash" or "ml")
     * @param reason Reason for removal (e.g., "Duplicate (distance: 5)")
     * @return true if successful, false otherwise
     */
    static bool moveToApplicationTrash(const QString& filePath,
                                      const QString& baseOutputDir,
                                      const QString& method,
                                      const QString& reason);

    /**
     * @brief Restore a file from application trash to its original location
     * @param trashedFilename Filename in trash (e.g., "slideRemoved_phash_Lecture01_001.jpg")
     * @param baseOutputDir Base output directory
     * @return true if successful, false otherwise
     */
    static bool restoreFromApplicationTrash(const QString& trashedFilename,
                                           const QString& baseOutputDir);

    /**
     * @brief Empty the application trash
     * @param baseOutputDir Base output directory
     * @param moveToSystemTrash If true, move to system trash; if false, delete permanently
     * @return Number of files successfully removed
     */
    static int emptyApplicationTrash(const QString& baseOutputDir, bool moveToSystemTrash);

    /**
     * @brief Get the number of items in application trash
     * @param baseOutputDir Base output directory
     * @return Number of trashed items
     */
    static int getTrashCount(const QString& baseOutputDir);

    /**
     * @brief Get the application trash directory path
     * @param baseOutputDir Base output directory
     * @return Path to .extractorTrash/ directory
     */
    static QString getTrashDirectory(const QString& baseOutputDir);

    /**
     * @brief Encode a filename for application trash
     * @param originalPath Original file path (e.g., ".../slides_Lecture01/slide_Lecture01_001.jpg")
     * @param method Removal method ("phash" or "ml")
     * @return Encoded filename (e.g., "slideRemoved_phash_Lecture01_001.jpg")
     */
    static QString encodeTrashFilename(const QString& originalPath, const QString& method);

    /**
     * @brief Decode a trashed filename to extract metadata
     * @param trashedFilename Trashed filename (e.g., "slideRemoved_phash_Lecture01_001.jpg")
     * @param videoName Output: video name (e.g., "Lecture01")
     * @param slideIndex Output: slide index (e.g., "001")
     * @param method Output: removal method ("phash" or "ml")
     * @return true if successfully decoded, false if format is invalid
     */
    static bool decodeTrashFilename(const QString& trashedFilename,
                                   QString& videoName,
                                   QString& slideIndex,
                                   QString& method);

    /**
     * @brief Clean up old trash entries based on retention days
     * @param baseOutputDir Base output directory
     * @param retentionDays Delete entries older than this many days
     * @return Number of files removed
     */
    static int cleanupOldEntries(const QString& baseOutputDir, int retentionDays);
};

#endif // TRASHMANAGER_H
