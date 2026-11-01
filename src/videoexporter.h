#ifndef VIDEOEXPORTER_H
#define VIDEOEXPORTER_H

#include <QString>
#include <QList>
#include <functional>

struct TimelineData;

/**
 * @brief Settings for turning a slides folder back into a video.
 *
 * Not persisted: like the PDF settings, the Slides Export dialog starts from
 * these defaults each time, and the CLI uses them as-is.
 */
struct VideoExportOptions {
    enum class AudioMode { None, Copy, Compress };

    int targetHeight = 1080;    // canvas height; 0 = size of the slides themselves
    int crf = 23;               // x264 constant quality (lower = better/larger)
    int repeatSeconds = 120;    // re-send the current slide this often; 0 = only at changes

    AudioMode audioMode = AudioMode::Compress;
    QString audioSourcePath;    // original video or an audio file; empty = silent video
    int audioBitrateKbps = 48;  // Compress only
    int audioChannels = 1;      // Compress only: 1 = mono, 2 = stereo
    int audioSampleRate = 0;    // Compress only: 0 = keep the source rate (max 48 kHz)

    static QString audioModeToString(AudioMode mode);
    static AudioMode audioModeFromString(const QString& s, AudioMode fallback);
};

struct VideoExportResult {
    bool ok = false;
    bool cancelled = false;
    QString error;
    QString outputPath;
    int frames = 0;             // encoded video frames
    int chapters = 0;
    double durationSeconds = 0.0;
    QString audioInfo;          // e.g. "copied (aac)", "AAC 48 kbps mono", "none"
    QStringList warnings;
};

/**
 * @brief Builds an MP4 from a slides folder and its timeline.json.
 *
 * Each slide is shown from its timeline changeAt until the next change:
 *  - canonical event -> its slide image
 *  - duplicate event (slide shown again) -> the kept copy (duplicateOf)
 *  - gap event (ML "not a slide", exclusion, manual trash) -> the previous
 *    slide stays on screen
 *
 * Video is H.264 (x264) at a variable frame rate: one frame per slide change,
 * plus an optional repeat of the current slide every repeatSeconds so players
 * that seek to the next frame are never far off. Each slide gets a chapter
 * named after its PDF page number. Audio is copied from the source, or
 * re-encoded to AAC, or left out.
 */
class VideoExporter
{
public:
    /** One stretch of the output: [start, next start) shows `file` (empty = black). */
    struct Segment {
        double start = 0.0;     // seconds in source media time
        QString file;           // basename inside the slides folder
    };

    /** @return false if fraction progress callback asks to cancel. */
    using ProgressFn = std::function<bool(double fraction)>;

    static VideoExportResult exportFolder(const QString& slidesDir,
                                          const QString& outputPath,
                                          const VideoExportOptions& options,
                                          const ProgressFn& progress = ProgressFn());

    /** Map timeline events to what is on screen (see class comment). */
    static QList<Segment> buildSegments(const TimelineData& timeline, const QString& slidesDir);

    /** Slide images in PDF page order (natural sort), basenames. */
    static QStringList slideImagesInOrder(const QString& slidesDir);

    /** True if the folder has a timeline.json with at least one event. */
    static bool hasTimeline(const QString& slidesDir);

    /** Source video recorded in timeline.json, if it still exists; else empty. */
    static QString recordedSourceVideo(const QString& slidesDir);

    /** True if this FFmpeg build has an H.264 encoder. */
    static bool isAvailable();
};

#endif // VIDEOEXPORTER_H
