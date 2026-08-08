#ifndef TIMELINEMETADATA_H
#define TIMELINEMETADATA_H

#include <QString>
#include <QList>
#include <QMap>
#include <QJsonObject>
#include <QMutex>
#include <functional>

/**
 * @brief Immutable capture log entry for timeline.json (schema v1).
 */
struct SlideCaptureEvent {
    QString id;            // evt_<unix_ms>_<alnum>
    double changeAt = 0.0; // media PTS seconds (transition into content)
    double confirmedAt = 0.0; // media PTS seconds (accept / save)
    QString initialFile;   // basename written at capture (never null in this build)
};

/**
 * @brief Mutable display resolution for one event id.
 */
struct SlideResolution {
    QString state;       // "canonical" | "duplicate" | "gap"
    QString file;        // state == canonical
    QString duplicateOf; // state == duplicate (first-kept basename)
    QString gapReason;   // state == gap: ai_filtered | exclusion | manual_trash
};

/**
 * @brief Full timeline document for one slides folder.
 */
struct TimelineData {
    int version = 1;
    QString extractor = QStringLiteral("qt");
    QString createdAt;
    QString updatedAt;
    QList<SlideCaptureEvent> events;
    QMap<QString, SlideResolution> resolutions; // eventId -> resolution
};

/**
 * @brief Read/write manager for <slidesDir>/timeline.json
 *
 * Mirrors TrashMetadata / CropMetadata style. mark* no-ops when the file is
 * absent so post-process/review stay silent without --write-timeline / GUI off.
 * Never deletes events; only mutates resolutions.
 */
class TimelineMetadata
{
public:
    static QString getTimelinePath(const QString& slidesDir);

    static bool load(const QString& slidesDir, TimelineData& data);
    static bool save(const QString& slidesDir, const TimelineData& data);

    /** Append confirmed capture + canonical resolution (creates file if needed). */
    static bool addConfirmedCapture(const QString& slidesDir,
                                    double changeAt,
                                    double confirmedAt,
                                    const QString& initialFile);

    /** Mark events whose initialFile matches trashed basename as duplicate. */
    static bool markDuplicate(const QString& slidesDir,
                              const QString& trashedFileBasename,
                              const QString& keptFileBasename);

    /** Mark events whose initialFile matches as gap (ai_filtered / exclusion / manual_trash). */
    static bool markGap(const QString& slidesDir,
                        const QString& trashedFileBasename,
                        const QString& gapReason);

    /** Restore events whose initialFile matches back to canonical. */
    static bool markRestore(const QString& slidesDir,
                            const QString& restoredFileBasename);

    static QString generateEventId();

private:
    static QJsonObject eventToJson(const SlideCaptureEvent& event);
    static SlideCaptureEvent jsonToEvent(const QJsonObject& json);
    static QJsonObject resolutionToJson(const SlideResolution& res);
    static SlideResolution jsonToResolution(const QJsonObject& json);

    static bool mutateMatchingInitialFile(
        const QString& slidesDir,
        const QString& initialFileBasename,
        const std::function<void(SlideResolution&)>& mutator,
        bool requireExistingFile);

    static QMutex s_mutex;
};

#endif // TIMELINEMETADATA_H
