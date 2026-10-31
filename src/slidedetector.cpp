#include "slidedetector.h"
#include <QElapsedTimer>
#include <algorithm>

namespace {

/**
 * The two-stage detector as a streaming state machine.
 *
 * scores[k] compares working frame k with frame k+1. A score below the
 * threshold opens a candidate at frame k+1; the candidate is saved once
 * (verificationCount - 1) further scores stay above the threshold, i.e. once
 * verificationCount identical frames have been seen. The saved frame is the
 * last of those (content has settled). A new change during verification
 * restarts it at the new frame.
 *
 * All state lives in ProcessingState, so a chunk boundary is invisible to the
 * algorithm.
 *
 * @param pts  pts(localFrame) -> media seconds
 * @param save save(localFrame, changeAt)
 */
template <typename PtsFn, typename SaveFn>
void runDetection(const std::vector<double>& scores, ProcessingState& state,
                  double ssimThreshold, int verificationCount,
                  PtsFn pts, SaveFn save)
{
    for (size_t k = 0; k < scores.size(); ++k) {
        const int frame = static_cast<int>(k) + 1;
        if (scores[k] < ssimThreshold) {
            // Change: (re)start verification at this frame
            state.pendingChangeAt = pts(frame);
            state.candidateFrameCount = 1;
            state.verifyRemaining = verificationCount - 1;
        } else if (state.verifyRemaining > 0) {
            --state.verifyRemaining;
            ++state.candidateFrameCount;
        } else {
            continue;
        }

        if (state.verifyRemaining == 0) {
            save(frame, state.pendingChangeAt);
            state.candidateFrameCount = 0;
        }
    }
}

/**
 * End of video: a candidate that ran out of frames while still stable is kept
 * if it was seen at least twice, so a short final slide is not lost. A single
 * trailing frame (often a fade or end card) is not kept.
 * @return true if the last frame should be saved
 */
bool shouldSaveTrailingCandidate(const ProcessingState& state)
{
    return state.verifyRemaining > 0 && state.candidateFrameCount >= 2;
}

} // namespace

SlideDetector::SlideDetector(QObject *parent)
    : QObject(parent)
{
}

SlideDetectionResult SlideDetector::detectSlides(const std::vector<std::string>& framePaths,
                                               double ssimThreshold,
                                               int verificationCount)
{
    SlideDetectionResult result;
    QElapsedTimer timer;
    timer.start();

    if (framePaths.empty()) {
        return result;
    }

    result.ssimScores = calculateSSIMScores(framePaths);
    for (int index : extractSlidesFromCount(static_cast<int>(framePaths.size()), result.ssimScores,
                                            ssimThreshold, verificationCount)) {
        result.selectedSlides.push_back(framePaths[index]);
    }

    result.totalFramesProcessed = static_cast<int>(framePaths.size());
    result.processingTimeSeconds = timer.elapsed() / 1000.0;
    return result;
}

SlideDetectionResult SlideDetector::detectSlidesFromFrames(const std::vector<cv::Mat>& frames,
                                                         double ssimThreshold,
                                                         int verificationCount)
{
    return detectSlidesFromFramesWithConfig(frames, ssimThreshold, verificationCount, true, 480, 270);
}

SlideDetectionResult SlideDetector::detectSlidesFromFramesWithConfig(const std::vector<cv::Mat>& frames,
                                                                   double ssimThreshold,
                                                                   int verificationCount,
                                                                   bool enableDownsampling,
                                                                   int downsampleWidth,
                                                                   int downsampleHeight)
{
    SlideDetectionResult result;
    QElapsedTimer timer;
    timer.start();

    if (frames.empty()) {
        return result;
    }

    result.ssimScores = calculateSSIMScoresFromFrames(frames, enableDownsampling, downsampleWidth, downsampleHeight);
    result.selectedSlideIndices = extractSlidesFromCount(static_cast<int>(frames.size()),
                                                       result.ssimScores,
                                                       ssimThreshold,
                                                       verificationCount);

    result.totalFramesProcessed = static_cast<int>(frames.size());
    result.processingTimeSeconds = timer.elapsed() / 1000.0;
    return result;
}

SlideDetectionResult SlideDetector::detectSlidesFromChunk(const std::vector<cv::Mat>& newFrames,
                                                        const std::vector<double>& newTimestamps,
                                                        ProcessingState& state,
                                                        bool isLastChunk,
                                                        double ssimThreshold,
                                                        int verificationCount,
                                                        bool enableDownsampling,
                                                        int downsampleWidth,
                                                        int downsampleHeight)
{
    SlideDetectionResult result;
    QElapsedTimer timer;
    timer.start();

    const int n = static_cast<int>(newFrames.size());
    if (n == 0) {
        return result;
    }
    verificationCount = std::max(1, verificationCount);

    // Working frames: [previous chunk's last frame] + newFrames. `base` is the
    // working index of newFrames[0].
    const bool firstChunk = state.isFirstChunk();
    const int base = firstChunk ? 0 : 1;

    std::vector<cv::Mat> prepared = prepareFrames(newFrames, enableDownsampling, downsampleWidth, downsampleHeight);
    std::vector<const cv::Mat*> working;
    working.reserve(n + 1);
    if (!firstChunk) {
        working.push_back(&state.lastFramePrepared);
    }
    for (const cv::Mat& m : prepared) {
        working.push_back(&m);
    }
    result.ssimScores = scoreAdjacent(working);

    auto ptsOfNew = [&](int i) -> double {
        if (i >= 0 && i < static_cast<int>(newTimestamps.size())) {
            return newTimestamps[static_cast<size_t>(i)];
        }
        return newTimestamps.empty() ? state.lastFrameTimestamp : newTimestamps.back();
    };
    // Working index -> media time / global frame index
    auto pts = [&](int w) { return w < base ? state.lastFrameTimestamp : ptsOfNew(w - base); };
    auto save = [&](int w, double changeAt) {
        const int globalIndex = state.globalFrameOffset + (w - base);
        state.savedSlideIndices.push_back(globalIndex);
        state.lastStableIndex = globalIndex;
        result.selectedSlideIndices.push_back(globalIndex);
        result.confirmedSlideTimings.push_back(ConfirmedSlideTiming{globalIndex, changeAt, pts(w)});
    };

    // The very first frame of the video is always a slide
    if (firstChunk) {
        save(0, pts(0));
    }

    runDetection(result.ssimScores, state, ssimThreshold, verificationCount, pts, save);

    if (isLastChunk && shouldSaveTrailingCandidate(state)) {
        save(base + n - 1, state.pendingChangeAt);
        state.verifyRemaining = 0;
        state.candidateFrameCount = 0;
    }

    // Carry only the prepared (small, grayscale) last frame into the next chunk
    state.lastFramePrepared = prepared.back();
    state.lastFrameGlobalIndex = state.globalFrameOffset + n - 1;
    state.lastFrameTimestamp = ptsOfNew(n - 1);

    result.totalFramesProcessed = n;
    result.processingTimeSeconds = timer.elapsed() / 1000.0;
    return result;
}

std::vector<int> SlideDetector::extractSlidesFromCount(int frameCount,
                                                     const std::vector<double>& scores,
                                                     double ssimThreshold,
                                                     int verificationCount)
{
    std::vector<int> saved;
    if (frameCount <= 0) {
        return saved;
    }

    ProcessingState state;
    auto pts = [](int frame) { return static_cast<double>(frame); };
    auto save = [&](int frame, double) { saved.push_back(frame); };

    save(0, 0.0);
    runDetection(scores, state, ssimThreshold, std::max(1, verificationCount), pts, save);
    if (shouldSaveTrailingCandidate(state)) {
        save(frameCount - 1, 0.0);
    }
    return saved;
}

std::vector<cv::Mat> SlideDetector::prepareFrames(const std::vector<cv::Mat>& frames,
                                                  bool enableDownsampling,
                                                  int downsampleWidth,
                                                  int downsampleHeight)
{
    std::vector<cv::Mat> prepared(frames.size());
    cv::parallel_for_(cv::Range(0, static_cast<int>(frames.size())), [&](const cv::Range& r) {
        for (int i = r.start; i < r.end; ++i) {
            prepared[i] = SSIMCalculator::prepareForSSIM(frames[i], enableDownsampling,
                                                         downsampleWidth, downsampleHeight);
        }
    });
    return prepared;
}

std::vector<double> SlideDetector::scoreAdjacent(const std::vector<const cv::Mat*>& prepared)
{
    if (prepared.size() < 2) {
        return {};
    }
    std::vector<double> scores(prepared.size() - 1);
    cv::parallel_for_(cv::Range(0, static_cast<int>(scores.size())), [&](const cv::Range& r) {
        for (int i = r.start; i < r.end; ++i) {
            scores[i] = SSIMCalculator::globalSSIMFromGray(*prepared[i], *prepared[i + 1]);
        }
    });
    return scores;
}

std::vector<double> SlideDetector::calculateSSIMScores(const std::vector<std::string>& framePaths)
{
    std::vector<double> scores;

    if (framePaths.size() < 2) {
        return scores;
    }

    // Prepare tasks for multi-threaded SSIM calculation
    std::vector<SSIMTask> tasks;
    tasks.reserve(framePaths.size() - 1);

    for (size_t i = 0; i < framePaths.size() - 1; ++i) {
        SSIMTask task;
        task.index = static_cast<int>(i);
        task.img1Path = framePaths[i];
        task.img2Path = framePaths[i + 1];
        task.useMatInput = false;
        task.enableDownsampling = true;
        task.downsampleWidth = 480;
        task.downsampleHeight = 270;
        tasks.push_back(task);
    }

    connect(&m_ssimCalculator, &SSIMCalculator::calculationProgress,
            this, &SlideDetector::ssimCalculationProgress);
    std::vector<SSIMResult> results = m_ssimCalculator.calculateMultiThreadedSSIM(tasks);
    disconnect(&m_ssimCalculator, &SSIMCalculator::calculationProgress,
               this, &SlideDetector::ssimCalculationProgress);

    scores.reserve(results.size());
    for (const auto& result : results) {
        scores.push_back(result.score);
    }

    return scores;
}

std::vector<double> SlideDetector::calculateSSIMScoresFromFrames(const std::vector<cv::Mat>& frames)
{
    return calculateSSIMScoresFromFrames(frames, true, 480, 270);
}

std::vector<double> SlideDetector::calculateSSIMScoresFromFrames(const std::vector<cv::Mat>& frames,
                                                               bool enableDownsampling,
                                                               int downsampleWidth,
                                                               int downsampleHeight)
{
    std::vector<cv::Mat> prepared = prepareFrames(frames, enableDownsampling, downsampleWidth, downsampleHeight);
    std::vector<const cv::Mat*> ptrs;
    ptrs.reserve(prepared.size());
    for (const cv::Mat& m : prepared) {
        ptrs.push_back(&m);
    }
    return scoreAdjacent(ptrs);
}
