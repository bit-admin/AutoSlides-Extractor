#include "timelinemetadata.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>

QMutex TimelineMetadata::s_mutex;

QString TimelineMetadata::getTimelinePath(const QString& slidesDir)
{
    return QDir(slidesDir).filePath(QStringLiteral("timeline.json"));
}

QString TimelineMetadata::generateEventId()
{
    const qint64 ms = QDateTime::currentMSecsSinceEpoch();
    // 6 hex chars from a 24-bit random value
    const quint32 r = QRandomGenerator::global()->bounded(0x1000000);
    return QStringLiteral("evt_%1_%2")
        .arg(ms)
        .arg(r, 6, 16, QLatin1Char('0'));
}

static QString isoNowUtc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QJsonObject TimelineMetadata::eventToJson(const SlideCaptureEvent& event)
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), event.id);
    o.insert(QStringLiteral("changeAt"), event.changeAt);
    o.insert(QStringLiteral("confirmedAt"), event.confirmedAt);
    o.insert(QStringLiteral("initialFile"), event.initialFile);
    return o;
}

SlideCaptureEvent TimelineMetadata::jsonToEvent(const QJsonObject& json)
{
    SlideCaptureEvent e;
    e.id = json.value(QStringLiteral("id")).toString();
    e.changeAt = json.value(QStringLiteral("changeAt")).toDouble();
    e.confirmedAt = json.value(QStringLiteral("confirmedAt")).toDouble();
    // initialFile may be JSON null in future unstable-gap events
    const QJsonValue init = json.value(QStringLiteral("initialFile"));
    if (init.isString()) {
        e.initialFile = init.toString();
    }
    return e;
}

QJsonObject TimelineMetadata::resolutionToJson(const SlideResolution& res)
{
    QJsonObject o;
    o.insert(QStringLiteral("state"), res.state);
    if (res.state == QLatin1String("canonical")) {
        o.insert(QStringLiteral("file"), res.file);
    } else if (res.state == QLatin1String("duplicate")) {
        o.insert(QStringLiteral("duplicateOf"), res.duplicateOf);
    } else if (res.state == QLatin1String("gap")) {
        o.insert(QStringLiteral("gapReason"), res.gapReason);
    }
    return o;
}

SlideResolution TimelineMetadata::jsonToResolution(const QJsonObject& json)
{
    SlideResolution r;
    r.state = json.value(QStringLiteral("state")).toString();
    r.file = json.value(QStringLiteral("file")).toString();
    r.duplicateOf = json.value(QStringLiteral("duplicateOf")).toString();
    r.gapReason = json.value(QStringLiteral("gapReason")).toString();
    return r;
}

bool TimelineMetadata::load(const QString& slidesDir, TimelineData& data)
{
    data = TimelineData();

    const QString path = getTimelinePath(slidesDir);
    QFile file(path);
    if (!file.exists()) {
        return true; // empty / absent is OK
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "TimelineMetadata: failed to open for reading:" << path;
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "TimelineMetadata: JSON parse error:" << parseError.errorString();
        return false;
    }

    const QJsonObject root = doc.object();
    data.version = root.value(QStringLiteral("version")).toInt(1);
    data.extractor = root.value(QStringLiteral("extractor")).toString(QStringLiteral("qt"));
    data.createdAt = root.value(QStringLiteral("createdAt")).toString();
    data.updatedAt = root.value(QStringLiteral("updatedAt")).toString();

    const QJsonArray eventsArr = root.value(QStringLiteral("events")).toArray();
    for (const QJsonValue& v : eventsArr) {
        if (!v.isObject()) {
            continue;
        }
        SlideCaptureEvent e = jsonToEvent(v.toObject());
        if (!e.id.isEmpty()) {
            data.events.append(e);
        }
    }

    const QJsonObject resObj = root.value(QStringLiteral("resolutions")).toObject();
    for (auto it = resObj.begin(); it != resObj.end(); ++it) {
        if (it.value().isObject()) {
            data.resolutions.insert(it.key(), jsonToResolution(it.value().toObject()));
        }
    }
    return true;
}

bool TimelineMetadata::save(const QString& slidesDir, const TimelineData& data)
{
    QDir dir(slidesDir);
    if (!dir.exists()) {
        if (!dir.mkpath(QStringLiteral("."))) {
            qWarning() << "TimelineMetadata: slides dir missing:" << slidesDir;
            return false;
        }
    }

    const QString path = getTimelinePath(slidesDir);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "TimelineMetadata: failed to open for writing:" << path;
        return false;
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), data.version);
    root.insert(QStringLiteral("extractor"), data.extractor.isEmpty()
                                                 ? QStringLiteral("qt")
                                                 : data.extractor);
    root.insert(QStringLiteral("createdAt"), data.createdAt);
    root.insert(QStringLiteral("updatedAt"), data.updatedAt);

    QJsonArray eventsArr;
    for (const SlideCaptureEvent& e : data.events) {
        eventsArr.append(eventToJson(e));
    }
    root.insert(QStringLiteral("events"), eventsArr);

    QJsonObject resObj;
    for (auto it = data.resolutions.constBegin(); it != data.resolutions.constEnd(); ++it) {
        resObj.insert(it.key(), resolutionToJson(it.value()));
    }
    root.insert(QStringLiteral("resolutions"), resObj);

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool TimelineMetadata::addConfirmedCapture(const QString& slidesDir,
                                           double changeAt,
                                           double confirmedAt,
                                           const QString& initialFile)
{
    if (slidesDir.isEmpty() || initialFile.isEmpty()) {
        return false;
    }

    QMutexLocker locker(&s_mutex);

    TimelineData data;
    if (!load(slidesDir, data)) {
        return false;
    }

    const QString now = isoNowUtc();
    if (data.createdAt.isEmpty()) {
        data.createdAt = now;
    }
    data.updatedAt = now;
    data.version = 1;
    data.extractor = QStringLiteral("qt");

    SlideCaptureEvent event;
    event.id = generateEventId();
    event.changeAt = changeAt;
    event.confirmedAt = confirmedAt >= changeAt ? confirmedAt : changeAt;
    event.initialFile = initialFile;
    data.events.append(event);

    SlideResolution res;
    res.state = QStringLiteral("canonical");
    res.file = initialFile;
    data.resolutions.insert(event.id, res);

    return save(slidesDir, data);
}

bool TimelineMetadata::mutateMatchingInitialFile(
    const QString& slidesDir,
    const QString& initialFileBasename,
    const std::function<void(SlideResolution&)>& mutator,
    bool requireExistingFile)
{
    if (slidesDir.isEmpty() || initialFileBasename.isEmpty()) {
        return true;
    }

    QMutexLocker locker(&s_mutex);

    const QString path = getTimelinePath(slidesDir);
    if (!QFile::exists(path)) {
        if (requireExistingFile) {
            return true; // no-op: capture never wrote a timeline
        }
        return true;
    }

    TimelineData data;
    if (!load(slidesDir, data)) {
        return false;
    }

    bool changed = false;
    for (const SlideCaptureEvent& e : data.events) {
        if (e.initialFile != initialFileBasename) {
            continue;
        }
        SlideResolution res = data.resolutions.value(e.id);
        mutator(res);
        data.resolutions.insert(e.id, res);
        changed = true;
    }

    if (!changed) {
        return true;
    }

    data.updatedAt = isoNowUtc();
    return save(slidesDir, data);
}

bool TimelineMetadata::markDuplicate(const QString& slidesDir,
                                     const QString& trashedFileBasename,
                                     const QString& keptFileBasename)
{
    return mutateMatchingInitialFile(
        slidesDir, trashedFileBasename,
        [keptFileBasename](SlideResolution& res) {
            res.state = QStringLiteral("duplicate");
            res.duplicateOf = keptFileBasename;
            res.file.clear();
            res.gapReason.clear();
        },
        true);
}

bool TimelineMetadata::markGap(const QString& slidesDir,
                               const QString& trashedFileBasename,
                               const QString& gapReason)
{
    return mutateMatchingInitialFile(
        slidesDir, trashedFileBasename,
        [gapReason](SlideResolution& res) {
            res.state = QStringLiteral("gap");
            res.gapReason = gapReason;
            res.file.clear();
            res.duplicateOf.clear();
        },
        true);
}

bool TimelineMetadata::markRestore(const QString& slidesDir,
                                   const QString& restoredFileBasename)
{
    return mutateMatchingInitialFile(
        slidesDir, restoredFileBasename,
        [restoredFileBasename](SlideResolution& res) {
            res.state = QStringLiteral("canonical");
            res.file = restoredFileBasename;
            res.duplicateOf.clear();
            res.gapReason.clear();
        },
        true);
}
