#include "videoexporter.h"
#include "timelinemetadata.h"
#include "imageiohelper.h"
#include "naturalsorter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------

QString VideoExportOptions::audioModeToString(AudioMode mode)
{
    switch (mode) {
    case AudioMode::None:     return QStringLiteral("none");
    case AudioMode::Copy:     return QStringLiteral("copy");
    case AudioMode::Aac:      return QStringLiteral("aac");
    case AudioMode::Opus:     return QStringLiteral("opus");
    }
    return QStringLiteral("aac");
}

VideoExportOptions::AudioMode VideoExportOptions::audioModeFromString(const QString& s, AudioMode fallback)
{
    const QString v = s.trimmed().toLower();
    if (v == QLatin1String("none"))     return AudioMode::None;
    if (v == QLatin1String("copy"))     return AudioMode::Copy;
    if (v == QLatin1String("aac") || v == QLatin1String("compress")) return AudioMode::Aac;
    if (v == QLatin1String("opus"))     return AudioMode::Opus;
    return fallback;
}

QList<int> VideoExportOptions::bitrateChoices(AudioMode mode)
{
    if (mode == AudioMode::Opus) {
        return {8, 12, 16, 24, 32, 48};
    }
    return {16, 24, 32, 48, 64, 96, 128};
}

int VideoExportOptions::defaultBitrateKbps(AudioMode mode)
{
    // Opus 16 kbps mono is clear speech; FFmpeg's AAC needs about 48 for the same.
    return mode == AudioMode::Opus ? 16 : 48;
}

int VideoExportOptions::minBitrateKbps(AudioMode mode)
{
    return mode == AudioMode::Opus ? 6 : 16;
}

int VideoExportOptions::maxBitrateKbps(AudioMode mode)
{
    return mode == AudioMode::Opus ? 256 : 320;
}

// ---------------------------------------------------------------------------
// Timeline -> segments
// ---------------------------------------------------------------------------

QStringList VideoExporter::slideImagesInOrder(const QString& slidesDir)
{
    // Same selection and order as the PDF export, so chapter N == PDF page N.
    QStringList images = QDir(slidesDir).entryList(
        QStringList() << "slide_*.jpg" << "slide_*.jpeg" << "slide_*.png", QDir::Files);
    NaturalSorter::sort(images);
    return images;
}

bool VideoExporter::hasTimeline(const QString& slidesDir)
{
    TimelineData data;
    return TimelineMetadata::load(slidesDir, data) && !data.events.isEmpty();
}

QString VideoExporter::recordedSourceVideo(const QString& slidesDir)
{
    TimelineData data;
    if (!TimelineMetadata::load(slidesDir, data) || data.sourcePath.isEmpty()) {
        return QString();
    }
    return QFileInfo::exists(data.sourcePath) ? data.sourcePath : QString();
}

QList<VideoExporter::Segment> VideoExporter::buildSegments(const TimelineData& timeline,
                                                           const QString& slidesDir)
{
    QList<SlideCaptureEvent> events = timeline.events;
    std::stable_sort(events.begin(), events.end(),
                     [](const SlideCaptureEvent& a, const SlideCaptureEvent& b) {
                         return a.changeAt < b.changeAt;
                     });

    const QDir dir(slidesDir);
    auto present = [&](const QString& name) {
        return !name.isEmpty() && QFileInfo::exists(dir.filePath(name));
    };

    QList<Segment> segments;
    QString current; // what is on screen; empty = nothing yet (black)
    for (const SlideCaptureEvent& e : events) {
        const auto it = timeline.resolutions.constFind(e.id);
        QString shown;
        if (it == timeline.resolutions.constEnd()) {
            shown = e.initialFile;                       // no resolution: as captured
        } else if (it->state == QLatin1String("canonical")) {
            shown = it->file.isEmpty() ? e.initialFile : it->file;
        } else if (it->state == QLatin1String("duplicate")) {
            shown = it->duplicateOf;                     // slide shown again
        }
        // gap (not a slide / excluded / trashed), or an image that is no longer
        // on disk: keep showing the previous slide.
        if (!present(shown)) {
            continue;
        }
        if (shown == current) {
            continue;
        }
        segments.append(Segment{e.changeAt, shown});
        current = shown;
    }
    // Removed slides at the very start have no previous slide: show black until
    // the first real one rather than pulling it earlier.
    if (!segments.isEmpty() && !events.isEmpty() && segments.first().start > events.first().changeAt) {
        segments.prepend(Segment{events.first().changeAt, QString()});
    }
    return segments;
}

// ---------------------------------------------------------------------------
// FFmpeg helpers
// ---------------------------------------------------------------------------

namespace {

QString avErr(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(err, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

struct FormatInputDeleter { void operator()(AVFormatContext* c) const { avformat_close_input(&c); } };
struct CodecCtxDeleter { void operator()(AVCodecContext* c) const { avcodec_free_context(&c); } };
struct FrameDeleter { void operator()(AVFrame* f) const { av_frame_free(&f); } };
struct PacketDeleter { void operator()(AVPacket* p) const { av_packet_free(&p); } };
struct SwrDeleter { void operator()(SwrContext* s) const { swr_free(&s); } };
struct FifoDeleter { void operator()(AVAudioFifo* f) const { av_audio_fifo_free(f); } };

using InputPtr = std::unique_ptr<AVFormatContext, FormatInputDeleter>;
using CodecCtxPtr = std::unique_ptr<AVCodecContext, CodecCtxDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;
using SwrPtr = std::unique_ptr<SwrContext, SwrDeleter>;
using FifoPtr = std::unique_ptr<AVAudioFifo, FifoDeleter>;

/** Output context; closes the file and frees chapters/streams. */
struct Output {
    AVFormatContext* ctx = nullptr;
    bool opened = false;
    ~Output()
    {
        if (ctx) {
            if (opened) {
                avio_closep(&ctx->pb);
            }
            avformat_free_context(ctx);
        }
    }
};

const AVCodec* findH264Encoder(bool* isX264)
{
    if (const AVCodec* c = avcodec_find_encoder_by_name("libx264")) {
        *isX264 = true;
        return c;
    }
    *isX264 = false;
    return avcodec_find_encoder(AV_CODEC_ID_H264); // e.g. VideoToolbox / Media Foundation
}

int nearestAacSampleRate(int rate)
{
    static const int kRates[] = {48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000};
    if (rate <= 0) {
        return 48000;
    }
    for (int r : kRates) {
        if (rate >= r) {
            return r;
        }
    }
    return 8000;
}

/** Fit `src` (BGR) inside a black w x h canvas, keeping its aspect ratio. */
cv::Mat letterbox(const cv::Mat& src, int w, int h)
{
    cv::Mat canvas(h, w, CV_8UC3, cv::Scalar(0, 0, 0));
    if (src.empty()) {
        return canvas;
    }
    const double scale = std::min(double(w) / src.cols, double(h) / src.rows);
    const int sw = std::max(1, int(std::lround(src.cols * scale)));
    const int sh = std::max(1, int(std::lround(src.rows * scale)));
    cv::Mat scaled;
    if (sw == src.cols && sh == src.rows) {
        scaled = src;
    } else {
        cv::resize(src, scaled, cv::Size(sw, sh), 0, 0, scale < 1.0 ? cv::INTER_AREA : cv::INTER_CUBIC);
    }
    scaled.copyTo(canvas(cv::Rect((w - sw) / 2, (h - sh) / 2, sw, sh)));
    return canvas;
}

/** Canvas size: the most common slide size, scaled to targetHeight (never up). */
void chooseCanvas(const QDir& dir, const QList<VideoExporter::Segment>& segments,
                  int targetHeight, int* outW, int* outH)
{
    QHash<QString, qint64> sizeOfFile; // (w << 32 | h), 0 = unreadable; each image read once
    QHash<qint64, int> counts;         // size -> number of segments showing it
    qint64 best = 0;
    int bestCount = 0;
    for (const auto& seg : segments) {
        auto it = sizeOfFile.find(seg.file);
        if (it == sizeOfFile.end()) {
            const cv::Mat m = ImageIOHelper::imreadUnicode(dir.filePath(seg.file), cv::IMREAD_COLOR);
            it = sizeOfFile.insert(seg.file, m.empty() ? 0 : (qint64(m.cols) << 32) | qint64(m.rows));
        }
        if (it.value() == 0) {
            continue;
        }
        const int n = ++counts[it.value()];
        if (n > bestCount) {
            bestCount = n;
            best = it.value();
        }
    }
    int w = bestCount > 0 ? int(best >> 32) : 1920;
    int h = bestCount > 0 ? int(best & 0xffffffff) : 1080;
    if (targetHeight > 0 && targetHeight < h) {
        w = int(std::lround(double(w) * targetHeight / h));
        h = targetHeight;
    }
    *outW = std::max(2, w & ~1); // yuv420p needs even sizes
    *outH = std::max(2, h & ~1);
}

/** Audio input: demuxer + chosen stream, plus the time origin shared with the timeline. */
struct AudioInput {
    InputPtr fmt;
    int stream = -1;
    double originSeconds = 0.0; // container start time
    double endSeconds = -1.0;   // end of the audio, in source time
};

bool openAudio(const QString& path, AudioInput* in, QString* error)
{
    AVFormatContext* raw = nullptr;
    int ret = avformat_open_input(&raw, path.toUtf8().constData(), nullptr, nullptr);
    if (ret < 0) {
        *error = QStringLiteral("Cannot open audio source %1: %2").arg(QFileInfo(path).fileName(), avErr(ret));
        return false;
    }
    in->fmt.reset(raw);
    if ((ret = avformat_find_stream_info(raw, nullptr)) < 0) {
        *error = QStringLiteral("Cannot read %1: %2").arg(QFileInfo(path).fileName(), avErr(ret));
        return false;
    }
    in->stream = av_find_best_stream(raw, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (in->stream < 0) {
        *error = QStringLiteral("No audio track in %1").arg(QFileInfo(path).fileName());
        return false;
    }
    in->originSeconds = raw->start_time != AV_NOPTS_VALUE ? double(raw->start_time) / AV_TIME_BASE : 0.0;

    const AVStream* st = raw->streams[in->stream];
    if (st->duration != AV_NOPTS_VALUE && st->duration > 0) {
        const double start = st->start_time != AV_NOPTS_VALUE ? st->start_time * av_q2d(st->time_base)
                                                              : in->originSeconds;
        in->endSeconds = start + st->duration * av_q2d(st->time_base);
    } else if (raw->duration != AV_NOPTS_VALUE && raw->duration > 0) {
        in->endSeconds = in->originSeconds + double(raw->duration) / AV_TIME_BASE;
    }
    return true;
}

/** Encoded video packet kept in memory until it is interleaved with audio. */
struct VideoPacket {
    PacketPtr pkt;
    double time = 0.0; // output seconds
};

} // namespace

bool VideoExporter::isAvailable()
{
    bool x264 = false;
    return findH264Encoder(&x264) != nullptr;
}

bool VideoExporter::isOpusAvailable()
{
    return avcodec_find_encoder_by_name("libopus") != nullptr;
}

// ---------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------

VideoExportResult VideoExporter::exportFolder(const QString& slidesDir,
                                              const QString& outputPath,
                                              const VideoExportOptions& options,
                                              const ProgressFn& progress)
{
    VideoExportResult result;
    result.outputPath = outputPath;
    const QDir dir(slidesDir);
    Output out;

    auto fail = [&](const QString& message) {
        result.ok = false;
        result.error = message;
        if (out.opened) {
            // Only remove a file this export started writing (closed first: Windows)
            avio_closep(&out.ctx->pb);
            out.opened = false;
            QFile::remove(outputPath);
        }
        return result;
    };
    auto cancelled = [&]() {
        result.cancelled = true;
        return fail(QStringLiteral("Cancelled"));
    };

    // --- what to show, and when ---------------------------------------------
    TimelineData timeline;
    if (!TimelineMetadata::load(slidesDir, timeline) || timeline.events.isEmpty()) {
        return fail(QStringLiteral("No timeline.json in %1 (extract with \"Write timeline\" on)")
                        .arg(dir.dirName()));
    }
    const QList<Segment> segments = buildSegments(timeline, slidesDir);
    if (std::none_of(segments.begin(), segments.end(), [](const Segment& seg) { return !seg.file.isEmpty(); })) {
        return fail(QStringLiteral("No slide images left in %1").arg(dir.dirName()));
    }

    // --- audio source (sets the time origin and the end) -----------------------
    const bool wantAudio = options.audioMode != VideoExportOptions::AudioMode::None
                           && !options.audioSourcePath.isEmpty();
    AudioInput audio;
    double origin = timeline.sourcePath.isEmpty() ? 0.0 : timeline.sourceStartTime;
    if (wantAudio) {
        QString err;
        if (!openAudio(options.audioSourcePath, &audio, &err)) {
            return fail(err);
        }
        origin = audio.originSeconds;
    }

    double endTime = -1.0; // source time
    if (wantAudio && audio.endSeconds > 0.0) {
        endTime = audio.endSeconds;
    } else if (timeline.sourceDuration > 0.0) {
        endTime = timeline.sourceStartTime + timeline.sourceDuration;
    }
    const double lastStart = segments.last().start;
    if (endTime <= lastStart) {
        // Unknown length: hold the last slide for a while
        endTime = lastStart + std::max(10.0, double(options.repeatSeconds));
        if (!wantAudio) {
            result.warnings << QStringLiteral("Video length unknown; last slide held for %1 s")
                                   .arg(int(endTime - lastStart));
        }
    }
    const double outEnd = endTime - origin;

    // --- frame schedule (output seconds) ---------------------------------------
    struct ScheduledFrame { double t; int segment; };
    std::vector<ScheduledFrame> schedule;
    for (int i = 0; i < segments.size(); ++i) {
        double t = i == 0 ? 0.0 : std::max(0.0, segments[i].start - origin);
        const double segEnd = i + 1 < segments.size() ? std::max(0.0, segments[i + 1].start - origin) : outEnd;
        if (!schedule.empty() && t <= schedule.back().t) {
            t = schedule.back().t + 0.001; // keep timestamps strictly increasing
        }
        if (t >= segEnd && i + 1 < segments.size()) {
            continue; // zero-length (e.g. before the audio starts)
        }
        schedule.push_back({t, i});
        if (options.repeatSeconds > 0) {
            for (double r = t + options.repeatSeconds; r < segEnd - 0.5; r += options.repeatSeconds) {
                schedule.push_back({r, i});
            }
        }
    }
    if (schedule.empty()) {
        return fail(QStringLiteral("Nothing to encode"));
    }

    // --- output container ------------------------------------------------------
    int ret = avformat_alloc_output_context2(&out.ctx, nullptr, "mp4", outputPath.toUtf8().constData());
    if (ret < 0 || !out.ctx) {
        return fail(QStringLiteral("Cannot create MP4 muxer: %1").arg(avErr(ret)));
    }

    // --- video encoder ---------------------------------------------------------
    bool isX264 = false;
    const AVCodec* vcodec = findH264Encoder(&isX264);
    if (!vcodec) {
        return fail(QStringLiteral("This build has no H.264 encoder"));
    }
    int width = 0, height = 0;
    chooseCanvas(dir, segments, options.targetHeight, &width, &height);

    CodecCtxPtr venc(avcodec_alloc_context3(vcodec));
    venc->width = width;
    venc->height = height;
    venc->pix_fmt = AV_PIX_FMT_YUV420P;
    venc->time_base = AVRational{1, 1000};
    venc->framerate = AVRational{1, 1};
    venc->max_b_frames = 0;
    venc->color_range = AVCOL_RANGE_MPEG;
    venc->color_primaries = AVCOL_PRI_BT709;
    venc->color_trc = AVCOL_TRC_BT709;
    venc->colorspace = AVCOL_SPC_BT709;
    if (out.ctx->oformat->flags & AVFMT_GLOBALHEADER) {
        venc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    if (isX264) {
        av_opt_set(venc->priv_data, "preset", "slow", 0);
        av_opt_set(venc->priv_data, "tune", "stillimage", 0);
        av_opt_set_double(venc->priv_data, "crf", options.crf, 0);
        // Keyframes come from scene cuts (slide changes); repeats are tiny P-frames.
        av_opt_set(venc->priv_data, "x264-params", "keyint=600:min-keyint=1:scenecut=40", 0);
    } else {
        venc->bit_rate = 1'500'000;
        venc->gop_size = 600;
        result.warnings << QStringLiteral("libx264 not available; used %1").arg(QString::fromUtf8(vcodec->name));
    }
    if ((ret = avcodec_open2(venc.get(), vcodec, nullptr)) < 0) {
        return fail(QStringLiteral("Cannot open H.264 encoder: %1").arg(avErr(ret)));
    }
    AVStream* vst = avformat_new_stream(out.ctx, nullptr);
    avcodec_parameters_from_context(vst->codecpar, venc.get());
    vst->time_base = venc->time_base;

    // --- audio stream ----------------------------------------------------------
    AVStream* ast = nullptr;
    bool copyAudio = false;
    CodecCtxPtr adec, aenc;
    SwrPtr swr;
    FifoPtr fifo;
    if (wantAudio) {
        const AVStream* inSt = audio.fmt->streams[audio.stream];
        copyAudio = options.audioMode == VideoExportOptions::AudioMode::Copy
                    && avformat_query_codec(out.ctx->oformat, inSt->codecpar->codec_id, FF_COMPLIANCE_NORMAL) == 1;
        if (options.audioMode == VideoExportOptions::AudioMode::Copy && !copyAudio) {
            result.warnings << QStringLiteral("%1 audio cannot go in MP4; re-encoded as AAC 48 kbps")
                                   .arg(QString::fromUtf8(avcodec_get_name(inSt->codecpar->codec_id)));
        }

        ast = avformat_new_stream(out.ctx, nullptr);
        if (copyAudio) {
            avcodec_parameters_copy(ast->codecpar, inSt->codecpar);
            ast->codecpar->codec_tag = 0;
            ast->time_base = inSt->time_base;
            result.audioInfo = QStringLiteral("copied (%1)").arg(QString::fromUtf8(avcodec_get_name(inSt->codecpar->codec_id)));
        } else {
            // Copy that cannot go in MP4 falls back to AAC
            const bool opus = options.audioMode == VideoExportOptions::AudioMode::Opus;
            const VideoExportOptions::AudioMode codecMode = opus ? VideoExportOptions::AudioMode::Opus
                                                                 : VideoExportOptions::AudioMode::Aac;
            const char* codecName = opus ? "Opus" : "AAC";
            const AVCodec* dcodec = avcodec_find_decoder(inSt->codecpar->codec_id);
            const AVCodec* ecodec = opus ? avcodec_find_encoder_by_name("libopus")
                                         : avcodec_find_encoder(AV_CODEC_ID_AAC);
            if (!dcodec || !ecodec) {
                return fail(QStringLiteral("No decoder for %1 audio, or no %2 encoder")
                                .arg(QString::fromUtf8(avcodec_get_name(inSt->codecpar->codec_id)),
                                     QString::fromLatin1(codecName)));
            }
            adec.reset(avcodec_alloc_context3(dcodec));
            avcodec_parameters_to_context(adec.get(), inSt->codecpar);
            adec->pkt_timebase = inSt->time_base;
            if ((ret = avcodec_open2(adec.get(), dcodec, nullptr)) < 0) {
                return fail(QStringLiteral("Cannot open audio decoder: %1").arg(avErr(ret)));
            }

            const int channels = std::clamp(options.audioChannels, 1, 2);
            // Opus runs at 48 kHz and narrows its audio bandwidth to suit the bitrate
            const int rate = opus ? 48000
                                  : nearestAacSampleRate(options.audioSampleRate > 0 ? options.audioSampleRate
                                                                                     : adec->sample_rate);
            const int requestedKbps = VideoExportOptions::isCompressed(options.audioMode)
                                          ? options.audioBitrateKbps
                                          : VideoExportOptions::defaultBitrateKbps(codecMode);
            const int kbps = std::clamp(requestedKbps,
                                        VideoExportOptions::minBitrateKbps(codecMode),
                                        VideoExportOptions::maxBitrateKbps(codecMode));
            aenc.reset(avcodec_alloc_context3(ecodec));
            av_channel_layout_default(&aenc->ch_layout, channels);
            aenc->sample_rate = rate;
            aenc->sample_fmt = opus ? AV_SAMPLE_FMT_FLT : AV_SAMPLE_FMT_FLTP;
            aenc->bit_rate = int64_t(kbps) * 1000;
            aenc->time_base = AVRational{1, rate};
            if (out.ctx->oformat->flags & AVFMT_GLOBALHEADER) {
                aenc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            }
            AVDictionary* encOpts = nullptr;
            if (opus) {
                // Tuned for speech. 60 ms frames: MP4 stores a per-packet entry, so
                // the default 20 ms frames add ~5 kbps, a third of a 16 kbps stream.
                av_dict_set(&encOpts, "application", "voip", 0);
                av_dict_set(&encOpts, "frame_duration", "60", 0);
            }
            ret = avcodec_open2(aenc.get(), ecodec, &encOpts);
            av_dict_free(&encOpts);
            if (ret < 0) {
                return fail(QStringLiteral("Cannot open %1 encoder: %2").arg(QString::fromLatin1(codecName), avErr(ret)));
            }
            avcodec_parameters_from_context(ast->codecpar, aenc.get());
            ast->time_base = aenc->time_base;

            AVChannelLayout inLayout;
            if (adec->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC || adec->ch_layout.nb_channels <= 0) {
                av_channel_layout_default(&inLayout, std::max(1, adec->ch_layout.nb_channels));
            } else {
                av_channel_layout_copy(&inLayout, &adec->ch_layout);
            }
            SwrContext* rawSwr = nullptr;
            ret = swr_alloc_set_opts2(&rawSwr, &aenc->ch_layout, aenc->sample_fmt, aenc->sample_rate,
                                      &inLayout, adec->sample_fmt, adec->sample_rate, 0, nullptr);
            av_channel_layout_uninit(&inLayout);
            swr.reset(rawSwr);
            if (ret < 0 || (ret = swr_init(swr.get())) < 0) {
                return fail(QStringLiteral("Cannot set up audio conversion: %1").arg(avErr(ret)));
            }
            fifo.reset(av_audio_fifo_alloc(aenc->sample_fmt, channels, aenc->frame_size > 0 ? aenc->frame_size * 4 : 4096));
            const QString layout = channels == 1 ? QStringLiteral("mono") : QStringLiteral("stereo");
            result.audioInfo = opus ? QStringLiteral("Opus %1 kbps %2").arg(kbps).arg(layout)
                                    : QStringLiteral("AAC %1 kbps %2, %3 kHz")
                                          .arg(kbps).arg(layout).arg(rate / 1000.0, 0, 'g', 3);
        }
    } else {
        result.audioInfo = QStringLiteral("none");
    }

    // --- chapters: one per slide shown, titled with its PDF page number --------
    {
        const QStringList pages = slideImagesInOrder(slidesDir);
        std::vector<const ScheduledFrame*> starts; // first frame of each segment
        for (const ScheduledFrame& f : schedule) {
            if (starts.empty() || starts.back()->segment != f.segment) {
                starts.push_back(&f);
            }
        }
        for (size_t i = 0; i < starts.size(); ++i) {
            const QString& file = segments[starts[i]->segment].file;
            if (file.isEmpty()) {
                continue; // black lead-in: no chapter
            }
            const int page = pages.indexOf(file);
            const QString title = page >= 0 ? QStringLiteral("Slide %1").arg(page + 1)
                                            : QFileInfo(file).completeBaseName();
            auto** grown = static_cast<AVChapter**>(
                av_realloc_array(out.ctx->chapters, out.ctx->nb_chapters + 1, sizeof(AVChapter*)));
            if (!grown) {
                break;
            }
            out.ctx->chapters = grown;
            auto* ch = static_cast<AVChapter*>(av_mallocz(sizeof(AVChapter)));
            if (!ch) {
                break;
            }
            ch->id = int64_t(i);
            ch->time_base = AVRational{1, 1000};
            ch->start = std::llround(starts[i]->t * 1000.0);
            ch->end = std::llround((i + 1 < starts.size() ? starts[i + 1]->t : outEnd) * 1000.0);
            av_dict_set(&ch->metadata, "title", title.toUtf8().constData(), 0);
            out.ctx->chapters[out.ctx->nb_chapters++] = ch;
        }
        result.chapters = int(out.ctx->nb_chapters);
    }

    {
        QString title = dir.dirName();
        if (title.startsWith(QLatin1String("slides_"))) {
            title = title.mid(7);
        }
        av_dict_set(&out.ctx->metadata, "title", title.toUtf8().constData(), 0);
    }

    // --- open file + header ----------------------------------------------------
    if ((ret = avio_open(&out.ctx->pb, outputPath.toUtf8().constData(), AVIO_FLAG_WRITE)) < 0) {
        return fail(QStringLiteral("Cannot write %1: %2").arg(outputPath, avErr(ret)));
    }
    out.opened = true;
    AVDictionary* muxOpts = nullptr;
    av_dict_set(&muxOpts, "movflags", "+faststart", 0);
    ret = avformat_write_header(out.ctx, &muxOpts);
    av_dict_free(&muxOpts);
    if (ret < 0) {
        return fail(QStringLiteral("Cannot write MP4 header: %1").arg(avErr(ret)));
    }

    const double videoWeight = wantAudio ? 0.2 : 1.0;
    auto report = [&](double fraction) { return !progress || progress(std::clamp(fraction, 0.0, 1.0)); };

    // --- encode video (kept in memory: a few MB at most) -----------------------
    std::vector<VideoPacket> videoPackets;
    std::map<int64_t, int64_t> durationByPts; // ms
    for (size_t i = 0; i < schedule.size(); ++i) {
        const int64_t pts = std::llround(schedule[i].t * 1000.0);
        const double next = i + 1 < schedule.size() ? schedule[i + 1].t : outEnd;
        durationByPts[pts] = std::max<int64_t>(1, std::llround(next * 1000.0) - pts);
    }

    auto drainVideo = [&]() -> int {
        for (;;) {
            PacketPtr pkt(av_packet_alloc());
            const int r = avcodec_receive_packet(venc.get(), pkt.get());
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) {
                return 0;
            }
            if (r < 0) {
                return r;
            }
            const auto it = durationByPts.find(pkt->pts);
            pkt->duration = it != durationByPts.end() ? it->second : 1;
            pkt->stream_index = vst->index;
            const double t = pkt->pts / 1000.0;
            av_packet_rescale_ts(pkt.get(), venc->time_base, vst->time_base);
            videoPackets.push_back(VideoPacket{std::move(pkt), t});
        }
    };

    SwsContext* sws = sws_getContext(width, height, AV_PIX_FMT_BGR24, width, height, AV_PIX_FMT_YUV420P,
                                     SWS_BICUBIC, nullptr, nullptr, nullptr);
    if (!sws) {
        return fail(QStringLiteral("Cannot set up colour conversion"));
    }
    std::unique_ptr<SwsContext, void (*)(SwsContext*)> swsGuard(sws, sws_freeContext);
    {
        // BT.709, limited range (what the stream is tagged as)
        const int* coeffs = sws_getCoefficients(SWS_CS_ITU709);
        sws_setColorspaceDetails(sws, coeffs, 1, coeffs, 0, 0, 1 << 16, 1 << 16);
    }

    FramePtr slideFrame;
    int slideFrameSegment = -1;
    for (size_t i = 0; i < schedule.size(); ++i) {
        const int seg = schedule[i].segment;
        if (seg != slideFrameSegment) {
            cv::Mat image; // empty file name = black (before the first slide)
            if (!segments[seg].file.isEmpty()) {
                image = ImageIOHelper::imreadUnicode(dir.filePath(segments[seg].file), cv::IMREAD_COLOR);
                if (image.empty()) {
                    result.warnings << QStringLiteral("Cannot read %1").arg(segments[seg].file);
                }
            }
            const cv::Mat canvas = letterbox(image, width, height);
            slideFrame.reset(av_frame_alloc());
            slideFrame->format = AV_PIX_FMT_YUV420P;
            slideFrame->width = width;
            slideFrame->height = height;
            if ((ret = av_frame_get_buffer(slideFrame.get(), 0)) < 0) {
                return fail(QStringLiteral("Out of memory: %1").arg(avErr(ret)));
            }
            const uint8_t* src[1] = {canvas.data};
            const int srcStride[1] = {static_cast<int>(canvas.step)};
            sws_scale(sws, src, srcStride, 0, height, slideFrame->data, slideFrame->linesize);
            slideFrameSegment = seg;
        }
        FramePtr frame(av_frame_clone(slideFrame.get()));
        frame->pts = std::llround(schedule[i].t * 1000.0);
        if ((ret = avcodec_send_frame(venc.get(), frame.get())) < 0 || (ret = drainVideo()) < 0) {
            return fail(QStringLiteral("Video encoding failed: %1").arg(avErr(ret)));
        }
        if (!report(videoWeight * double(i + 1) / double(schedule.size()))) {
            return cancelled();
        }
    }
    avcodec_send_frame(venc.get(), nullptr);
    if ((ret = drainVideo()) < 0) {
        return fail(QStringLiteral("Video encoding failed: %1").arg(avErr(ret)));
    }
    result.frames = int(videoPackets.size());

    // --- write: audio packets in order, video packets slotted in by time -------
    size_t nextVideo = 0;
    auto writeVideoUpTo = [&](double t) -> int {
        while (nextVideo < videoPackets.size() && videoPackets[nextVideo].time <= t) {
            const int r = av_interleaved_write_frame(out.ctx, videoPackets[nextVideo].pkt.get());
            ++nextVideo;
            if (r < 0) {
                return r;
            }
        }
        return 0;
    };

    if (wantAudio) {
        AVFormatContext* in = audio.fmt.get();
        const AVStream* inSt = in->streams[audio.stream];
        const AVRational inTb = inSt->time_base;
        const int64_t originInTb = av_rescale_q(int64_t(std::llround(origin * AV_TIME_BASE)),
                                                AVRational{1, AV_TIME_BASE}, inTb);
        const double span = std::max(1.0, outEnd);
        PacketPtr pkt(av_packet_alloc());
        FramePtr decoded(av_frame_alloc());
        int64_t nextAudioPts = -1;   // encoder time base (samples)
        int lastReported = -1;

        auto encodeFromFifo = [&](bool flush) -> int {
            const int frameSize = aenc->frame_size > 0 ? aenc->frame_size : 1024;
            while (av_audio_fifo_size(fifo.get()) >= frameSize
                   || (flush && av_audio_fifo_size(fifo.get()) > 0)) {
                const int n = std::min(frameSize, av_audio_fifo_size(fifo.get()));
                FramePtr f(av_frame_alloc());
                f->nb_samples = n;
                f->format = aenc->sample_fmt;
                f->sample_rate = aenc->sample_rate;
                av_channel_layout_copy(&f->ch_layout, &aenc->ch_layout);
                int r = av_frame_get_buffer(f.get(), 0);
                if (r < 0) return r;
                av_audio_fifo_read(fifo.get(), reinterpret_cast<void**>(f->data), n);
                f->pts = nextAudioPts;
                nextAudioPts += n;
                if ((r = avcodec_send_frame(aenc.get(), f.get())) < 0) return r;
                for (;;) {
                    PacketPtr opkt(av_packet_alloc());
                    r = avcodec_receive_packet(aenc.get(), opkt.get());
                    if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
                    if (r < 0) return r;
                    opkt->stream_index = ast->index;
                    av_packet_rescale_ts(opkt.get(), aenc->time_base, ast->time_base);
                    const double t = opkt->pts * av_q2d(ast->time_base);
                    if ((r = writeVideoUpTo(t)) < 0) return r;
                    if ((r = av_interleaved_write_frame(out.ctx, opkt.get())) < 0) return r;
                }
            }
            return 0;
        };

        // Output sample position (encoder rate) where the next converted sample goes
        int64_t queuedEnd = -1;

        auto queueSilence = [&](int64_t count) -> int {
            const int chunk = aenc->sample_rate; // 1 s at a time
            while (count > 0) {
                const int n = int(std::min<int64_t>(count, chunk));
                std::vector<uint8_t*> planes(size_t(aenc->ch_layout.nb_channels), nullptr);
                int r = av_samples_alloc(planes.data(), nullptr, aenc->ch_layout.nb_channels, n, aenc->sample_fmt, 0);
                if (r < 0) return r;
                av_samples_set_silence(planes.data(), 0, n, aenc->ch_layout.nb_channels, aenc->sample_fmt);
                av_audio_fifo_write(fifo.get(), reinterpret_cast<void**>(planes.data()), n);
                av_freep(&planes[0]);
                queuedEnd += n;
                count -= n;
                if ((r = encodeFromFifo(false)) < 0) return r;
            }
            return 0;
        };

        auto convertAndQueue = [&](const AVFrame* src) -> int {
            const int rate = aenc->sample_rate;
            const int64_t ts = src && src->best_effort_timestamp != AV_NOPTS_VALUE ? src->best_effort_timestamp
                                                                                   : AV_NOPTS_VALUE;
            if (nextAudioPts < 0) {
                // First samples: place them at their source time
                const double startSec = std::max(0.0, ((ts != AV_NOPTS_VALUE ? ts : originInTb) - originInTb)
                                                          * av_q2d(inTb));
                nextAudioPts = std::llround(startSec * rate);
                queuedEnd = nextAudioPts;
            }

            // Follow the source timestamps, as players and the slide timeline do.
            // Recorders drop audio and stretch timestamps to the wall clock (a
            // 99-min lecture: 3.1 s over ~150 holes); counting samples back to
            // back would let the voice run ahead of the slides. Fill holes with
            // silence and drop overlaps, beyond a 10 ms tolerance.
            int64_t dropSamples = 0;
            if (ts != AV_NOPTS_VALUE) {
                const int64_t expected = std::llround((ts - originInTb) * av_q2d(inTb) * rate);
                const int64_t diff = expected - (queuedEnd + swr_get_delay(swr.get(), rate));
                const int64_t tolerance = rate / 100;
                if (diff > tolerance) {
                    if (int r = queueSilence(diff); r < 0) return r;
                } else if (diff < -tolerance) {
                    dropSamples = -diff;
                }
            }

            const int maxOut = swr_get_out_samples(swr.get(), src ? src->nb_samples : 0);
            if (maxOut <= 0) return 0;
            std::vector<uint8_t*> planes(size_t(aenc->ch_layout.nb_channels), nullptr);
            int r = av_samples_alloc(planes.data(), nullptr, aenc->ch_layout.nb_channels, maxOut, aenc->sample_fmt, 0);
            if (r < 0) return r;
            const int got = swr_convert(swr.get(), planes.data(), maxOut,
                                        src ? const_cast<const uint8_t**>(src->extended_data) : nullptr,
                                        src ? src->nb_samples : 0);
            const int skip = int(std::min<int64_t>(dropSamples, std::max(got, 0)));
            if (got - skip > 0) {
                std::vector<uint8_t*> from(planes);
                const int bytesPerSample = av_get_bytes_per_sample(aenc->sample_fmt);
                if (av_sample_fmt_is_planar(aenc->sample_fmt)) {
                    for (uint8_t*& p : from) p += size_t(skip) * bytesPerSample;
                } else {
                    from[0] += size_t(skip) * bytesPerSample * aenc->ch_layout.nb_channels;
                }
                av_audio_fifo_write(fifo.get(), reinterpret_cast<void**>(from.data()), got - skip);
                queuedEnd += got - skip;
            }
            av_freep(&planes[0]);
            return got < 0 ? got : 0;
        };

        auto decodeAndEncode = [&](const AVPacket* p) -> int {
            int r = avcodec_send_packet(adec.get(), p);
            if (r < 0 && r != AVERROR(EAGAIN) && r != AVERROR_EOF) {
                return 0; // skip a corrupt packet
            }
            for (;;) {
                r = avcodec_receive_frame(adec.get(), decoded.get());
                if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return 0;
                if (r < 0) return 0;
                r = convertAndQueue(decoded.get());
                av_frame_unref(decoded.get());
                if (r < 0) return r;
                if ((r = encodeFromFifo(false)) < 0) return r;
            }
        };

        while ((ret = av_read_frame(in, pkt.get())) >= 0) {
            if (pkt->stream_index != audio.stream) {
                av_packet_unref(pkt.get());
                continue;
            }
            const int64_t ts = pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts;
            const double outTime = ts != AV_NOPTS_VALUE ? (ts - originInTb) * av_q2d(inTb) : 0.0;
            int r = 0;
            if (copyAudio) {
                if (pkt->pts != AV_NOPTS_VALUE) pkt->pts -= originInTb;
                if (pkt->dts != AV_NOPTS_VALUE) pkt->dts -= originInTb;
                pkt->stream_index = ast->index;
                av_packet_rescale_ts(pkt.get(), inTb, ast->time_base);
                pkt->pos = -1;
                if ((r = writeVideoUpTo(outTime)) >= 0) {
                    r = av_interleaved_write_frame(out.ctx, pkt.get());
                }
            } else {
                r = decodeAndEncode(pkt.get());
            }
            av_packet_unref(pkt.get());
            if (r < 0) {
                return fail(QStringLiteral("Audio processing failed: %1").arg(avErr(r)));
            }
            const int permille = int(1000.0 * outTime / span);
            if (permille != lastReported) {
                lastReported = permille;
                if (!report(videoWeight + (1.0 - videoWeight) * outTime / span)) {
                    return cancelled();
                }
            }
        }
        if (!copyAudio) {
            int r = decodeAndEncode(nullptr);            // drain decoder
            if (r >= 0) r = convertAndQueue(nullptr);   // drain resampler
            if (r >= 0) r = encodeFromFifo(true);       // last partial frame
            if (r >= 0) {
                avcodec_send_frame(aenc.get(), nullptr); // drain encoder
                for (;;) {
                    PacketPtr opkt(av_packet_alloc());
                    r = avcodec_receive_packet(aenc.get(), opkt.get());
                    if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) { r = 0; break; }
                    if (r < 0) break;
                    opkt->stream_index = ast->index;
                    av_packet_rescale_ts(opkt.get(), aenc->time_base, ast->time_base);
                    if ((r = writeVideoUpTo(opkt->pts * av_q2d(ast->time_base))) < 0) break;
                    if ((r = av_interleaved_write_frame(out.ctx, opkt.get())) < 0) break;
                }
            }
            if (r < 0) {
                return fail(QStringLiteral("Audio encoding failed: %1").arg(avErr(r)));
            }
        }
    }

    if ((ret = writeVideoUpTo(1e18)) < 0) {
        return fail(QStringLiteral("Writing video failed: %1").arg(avErr(ret)));
    }
    if ((ret = av_write_trailer(out.ctx)) < 0) {
        return fail(QStringLiteral("Cannot finish MP4: %1").arg(avErr(ret)));
    }

    result.ok = true;
    result.durationSeconds = outEnd;
    report(1.0);
    return result;
}
