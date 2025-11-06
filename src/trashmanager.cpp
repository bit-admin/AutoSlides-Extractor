#include "trashmanager.h"
#include <QFile>
#include <QFileInfo>

bool TrashManager::moveToTrash(const QString& filePath)
{
    if (!QFile::exists(filePath)) {
        return false;
    }

    // Use Qt's built-in moveToTrash function (available since Qt 5.15)
    return QFile::moveToTrash(filePath);
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
