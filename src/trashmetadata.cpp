#include "trashmetadata.h"
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QSet>

QString TrashMetadata::getMetadataPath(const QString& trashDir)
{
    return QDir(trashDir).filePath("metadata.json");
}

bool TrashMetadata::load(const QString& trashDir, QList<TrashEntry>& entries)
{
    entries.clear();

    QString metadataPath = getMetadataPath(trashDir);
    QFile file(metadataPath);

    // If file doesn't exist, that's okay (empty trash)
    if (!file.exists()) {
        return true;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "TrashMetadata: Failed to open metadata file for reading:" << metadataPath;
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "TrashMetadata: JSON parse error:" << parseError.errorString();
        return false;
    }

    if (!doc.isObject()) {
        qWarning() << "TrashMetadata: Root is not a JSON object";
        return false;
    }

    QJsonObject root = doc.object();
    QString version = root.value("version").toString("1.0");

    if (version != "1.0") {
        qWarning() << "TrashMetadata: Unsupported version:" << version;
        return false;
    }

    QJsonArray entriesArray = root.value("entries").toArray();
    for (const QJsonValue& value : entriesArray) {
        if (value.isObject()) {
            TrashEntry entry = jsonToEntry(value.toObject());
            if (!entry.trashedFilename.isEmpty()) {
                entries.append(entry);
            }
        }
    }

    return true;
}

bool TrashMetadata::save(const QString& trashDir, const QList<TrashEntry>& entries)
{
    // Ensure trash directory exists
    QDir dir(trashDir);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            qWarning() << "TrashMetadata: Failed to create trash directory:" << trashDir;
            return false;
        }
    }

    QString metadataPath = getMetadataPath(trashDir);
    QFile file(metadataPath);

    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "TrashMetadata: Failed to open metadata file for writing:" << metadataPath;
        return false;
    }

    // Build JSON document
    QJsonObject root;
    root["version"] = "1.0";

    QJsonArray entriesArray;
    for (const TrashEntry& entry : entries) {
        entriesArray.append(entryToJson(entry));
    }
    root["entries"] = entriesArray;

    QJsonDocument doc(root);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    return true;
}

bool TrashMetadata::addEntry(const QString& trashDir, const TrashEntry& entry)
{
    QList<TrashEntry> entries;
    if (!load(trashDir, entries)) {
        qWarning() << "TrashMetadata: Failed to load existing entries";
        return false;
    }

    entries.append(entry);
    return save(trashDir, entries);
}

bool TrashMetadata::removeEntry(const QString& trashDir, const QString& trashedFilename)
{
    QList<TrashEntry> entries;
    if (!load(trashDir, entries)) {
        qWarning() << "TrashMetadata: Failed to load existing entries";
        return false;
    }

    // Remove entry with matching filename
    int removed = 0;
    for (int i = entries.size() - 1; i >= 0; --i) {
        if (entries[i].trashedFilename == trashedFilename) {
            entries.removeAt(i);
            removed++;
        }
    }

    if (removed == 0) {
        qWarning() << "TrashMetadata: Entry not found:" << trashedFilename;
        return false;
    }

    return save(trashDir, entries);
}

QList<TrashEntry> TrashMetadata::getEntries(const QString& trashDir)
{
    QList<TrashEntry> entries;
    load(trashDir, entries);
    return entries;
}

QList<TrashEntry> TrashMetadata::filterByMethod(const QList<TrashEntry>& entries, const QString& method)
{
    if (method.isEmpty()) {
        return entries;
    }

    QList<TrashEntry> filtered;
    for (const TrashEntry& entry : entries) {
        if (entry.method == method) {
            filtered.append(entry);
        }
    }
    return filtered;
}

QList<TrashEntry> TrashMetadata::filterByVideo(const QList<TrashEntry>& entries, const QString& videoName)
{
    if (videoName.isEmpty()) {
        return entries;
    }

    QList<TrashEntry> filtered;
    for (const TrashEntry& entry : entries) {
        if (entry.videoName == videoName) {
            filtered.append(entry);
        }
    }
    return filtered;
}

QStringList TrashMetadata::getUniqueVideoNames(const QList<TrashEntry>& entries)
{
    QSet<QString> uniqueNames;
    for (const TrashEntry& entry : entries) {
        uniqueNames.insert(entry.videoName);
    }

    QStringList names = uniqueNames.values();
    names.sort();
    return names;
}

QJsonObject TrashMetadata::entryToJson(const TrashEntry& entry)
{
    QJsonObject json;
    json["trashedFilename"] = entry.trashedFilename;
    json["originalFolder"] = entry.originalFolder;
    json["videoName"] = entry.videoName;
    json["slideIndex"] = entry.slideIndex;
    json["method"] = entry.method;
    json["reason"] = entry.reason;
    json["timestamp"] = entry.timestamp.toString(Qt::ISODate);
    return json;
}

TrashEntry TrashMetadata::jsonToEntry(const QJsonObject& json)
{
    TrashEntry entry;
    entry.trashedFilename = json.value("trashedFilename").toString();
    entry.originalFolder = json.value("originalFolder").toString();
    entry.videoName = json.value("videoName").toString();
    entry.slideIndex = json.value("slideIndex").toString();
    entry.method = json.value("method").toString();
    entry.reason = json.value("reason").toString();
    entry.timestamp = QDateTime::fromString(json.value("timestamp").toString(), Qt::ISODate);
    return entry;
}
