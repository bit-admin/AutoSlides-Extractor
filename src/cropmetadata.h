#ifndef CROPMETADATA_H
#define CROPMETADATA_H

#include <QString>
#include <QList>
#include "cropentry.h"

class QJsonObject;

/**
 * @brief Manager for crop metadata JSON file.
 *
 * Handles loading, saving, and manipulating `<base>/.extractorCrop/metadata.json`,
 * which tracks pre-crop originals of cropped slide images.
 *
 * Mirrors the surface of TrashMetadata so behavior stays symmetric.
 */
class CropMetadata
{
public:
    static bool load(const QString& cropDir, QList<CropEntry>& entries);
    static bool save(const QString& cropDir, const QList<CropEntry>& entries);
    static bool addEntry(const QString& cropDir, const CropEntry& entry);
    static bool removeEntry(const QString& cropDir, const QString& backupFilename);
    static bool updateEntry(const QString& cropDir, const CropEntry& entry);
    static QList<CropEntry> getEntries(const QString& cropDir);

    static QString getMetadataPath(const QString& cropDir);

private:
    static QJsonObject entryToJson(const CropEntry& entry);
    static CropEntry jsonToEntry(const QJsonObject& json);
};

#endif // CROPMETADATA_H
