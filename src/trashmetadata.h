#ifndef TRASHMETADATA_H
#define TRASHMETADATA_H

#include <QString>
#include <QList>
#include "trashentry.h"

/**
 * @brief Manager for trash metadata JSON file
 *
 * Handles loading, saving, and manipulating the metadata.json file
 * that tracks all trashed slide images in the application trash.
 */
class TrashMetadata
{
public:
    /**
     * @brief Load trash entries from metadata.json
     * @param trashDir Path to .extractorTrash directory
     * @param entries Output list of trash entries
     * @return true if loaded successfully (or file doesn't exist yet), false on error
     */
    static bool load(const QString& trashDir, QList<TrashEntry>& entries);

    /**
     * @brief Save trash entries to metadata.json
     * @param trashDir Path to .extractorTrash directory
     * @param entries List of trash entries to save
     * @return true if saved successfully, false on error
     */
    static bool save(const QString& trashDir, const QList<TrashEntry>& entries);

    /**
     * @brief Add a new entry to metadata.json
     * @param trashDir Path to .extractorTrash directory
     * @param entry Entry to add
     * @return true if added successfully, false on error
     */
    static bool addEntry(const QString& trashDir, const TrashEntry& entry);

    /**
     * @brief Remove an entry from metadata.json
     * @param trashDir Path to .extractorTrash directory
     * @param trashedFilename Filename of the trashed file to remove
     * @return true if removed successfully, false on error
     */
    static bool removeEntry(const QString& trashDir, const QString& trashedFilename);

    /**
     * @brief Get all trash entries
     * @param trashDir Path to .extractorTrash directory
     * @return List of all trash entries (empty list if file doesn't exist or error)
     */
    static QList<TrashEntry> getEntries(const QString& trashDir);

    /**
     * @brief Filter entries by removal method
     * @param entries List of entries to filter
     * @param method Method to filter by ("phash" or "ml", empty string = all)
     * @return Filtered list of entries
     */
    static QList<TrashEntry> filterByMethod(const QList<TrashEntry>& entries, const QString& method);

    /**
     * @brief Filter entries by video name
     * @param entries List of entries to filter
     * @param videoName Video name to filter by (empty string = all)
     * @return Filtered list of entries
     */
    static QList<TrashEntry> filterByVideo(const QList<TrashEntry>& entries, const QString& videoName);

    /**
     * @brief Get list of unique video names from entries
     * @param entries List of entries
     * @return Sorted list of unique video names
     */
    static QStringList getUniqueVideoNames(const QList<TrashEntry>& entries);

    /**
     * @brief Get metadata file path
     * @param trashDir Path to .extractorTrash directory
     * @return Full path to metadata.json
     */
    static QString getMetadataPath(const QString& trashDir);

private:
    /**
     * @brief Convert TrashEntry to JSON object
     * @param entry Entry to convert
     * @return JSON object
     */
    static QJsonObject entryToJson(const TrashEntry& entry);

    /**
     * @brief Convert JSON object to TrashEntry
     * @param json JSON object
     * @return TrashEntry (may be invalid if JSON is malformed)
     */
    static TrashEntry jsonToEntry(const QJsonObject& json);
};

#endif // TRASHMETADATA_H
