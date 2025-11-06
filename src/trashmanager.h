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
};

#endif // TRASHMANAGER_H
