#include "cropmetadata.h"
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

QString CropMetadata::getMetadataPath(const QString& cropDir)
{
    return QDir(cropDir).filePath("metadata.json");
}

bool CropMetadata::load(const QString& cropDir, QList<CropEntry>& entries)
{
    entries.clear();

    QString metadataPath = getMetadataPath(cropDir);
    QFile file(metadataPath);

    if (!file.exists()) {
        return true;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "CropMetadata: Failed to open metadata file for reading:" << metadataPath;
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "CropMetadata: JSON parse error:" << parseError.errorString();
        return false;
    }

    if (!doc.isObject()) {
        qWarning() << "CropMetadata: Root is not a JSON object";
        return false;
    }

    QJsonObject root = doc.object();
    QString version = root.value("version").toString("1.0");

    // 1.0: base fields. 1.1: adds optional autoCropped (defaults false if missing).
    if (version != "1.0" && version != "1.1") {
        qWarning() << "CropMetadata: Unsupported version:" << version;
        return false;
    }

    QJsonArray entriesArray = root.value("entries").toArray();
    for (const QJsonValue& value : entriesArray) {
        if (value.isObject()) {
            CropEntry entry = jsonToEntry(value.toObject());
            if (!entry.backupFilename.isEmpty()) {
                entries.append(entry);
            }
        }
    }

    return true;
}

bool CropMetadata::save(const QString& cropDir, const QList<CropEntry>& entries)
{
    QDir dir(cropDir);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            qWarning() << "CropMetadata: Failed to create crop directory:" << cropDir;
            return false;
        }
    }

    QString metadataPath = getMetadataPath(cropDir);
    QFile file(metadataPath);

    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "CropMetadata: Failed to open metadata file for writing:" << metadataPath;
        return false;
    }

    QJsonObject root;
    root["version"] = "1.1";

    QJsonArray entriesArray;
    for (const CropEntry& entry : entries) {
        entriesArray.append(entryToJson(entry));
    }
    root["entries"] = entriesArray;

    QJsonDocument doc(root);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    return true;
}

bool CropMetadata::addEntry(const QString& cropDir, const CropEntry& entry)
{
    QList<CropEntry> entries;
    if (!load(cropDir, entries)) {
        return false;
    }

    entries.append(entry);
    return save(cropDir, entries);
}

bool CropMetadata::removeEntry(const QString& cropDir, const QString& backupFilename)
{
    QList<CropEntry> entries;
    if (!load(cropDir, entries)) {
        return false;
    }

    int removed = 0;
    for (int i = entries.size() - 1; i >= 0; --i) {
        if (entries[i].backupFilename == backupFilename) {
            entries.removeAt(i);
            removed++;
        }
    }

    if (removed == 0) {
        qWarning() << "CropMetadata: Entry not found:" << backupFilename;
        return false;
    }

    return save(cropDir, entries);
}

bool CropMetadata::updateEntry(const QString& cropDir, const CropEntry& entry)
{
    QList<CropEntry> entries;
    if (!load(cropDir, entries)) {
        return false;
    }

    bool replaced = false;
    for (int i = 0; i < entries.size(); ++i) {
        if (entries[i].backupFilename == entry.backupFilename) {
            entries[i] = entry;
            replaced = true;
            break;
        }
    }

    if (!replaced) {
        entries.append(entry);
    }

    return save(cropDir, entries);
}

QList<CropEntry> CropMetadata::getEntries(const QString& cropDir)
{
    QList<CropEntry> entries;
    load(cropDir, entries);
    return entries;
}

QJsonObject CropMetadata::entryToJson(const CropEntry& entry)
{
    QJsonObject json;
    json["backupFilename"] = entry.backupFilename;
    json["originalFolder"] = entry.originalFolder;
    json["videoName"] = entry.videoName;
    json["slideIndex"] = entry.slideIndex;
    json["cropX"] = entry.cropX;
    json["cropY"] = entry.cropY;
    json["cropW"] = entry.cropW;
    json["cropH"] = entry.cropH;
    json["timestamp"] = entry.timestamp.toString(Qt::ISODate);
    json["autoCropped"] = entry.autoCropped;
    return json;
}

CropEntry CropMetadata::jsonToEntry(const QJsonObject& json)
{
    CropEntry entry;
    entry.backupFilename = json.value("backupFilename").toString();
    entry.originalFolder = json.value("originalFolder").toString();
    entry.videoName = json.value("videoName").toString();
    entry.slideIndex = json.value("slideIndex").toString();
    entry.cropX = json.value("cropX").toInt();
    entry.cropY = json.value("cropY").toInt();
    entry.cropW = json.value("cropW").toInt();
    entry.cropH = json.value("cropH").toInt();
    entry.timestamp = QDateTime::fromString(json.value("timestamp").toString(), Qt::ISODate);
    // Missing key (schema 1.0) → false.
    entry.autoCropped = json.value("autoCropped").toBool(false);
    return entry;
}
