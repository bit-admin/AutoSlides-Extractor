#include "slidedetector.h"
#include "memoryoptimizer.h"
#include <QDebug>
#include <QElapsedTimer>
#include <algorithm>
#include <iostream>

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
        result.processingTimeSeconds = timer.elapsed() / 1000.0;
        return result;
    }

    // Step 1: Calculate SSIM scores for all adjacent frame pairs
    result.ssimScores = calculateSSIMScores(framePaths);

    // Step 2: Extract slides using the two-stage algorithm
    std::vector<int> selectedIndices = extractSlides(framePaths, result.ssimScores, ssimThreshold, verificationCount);

    // Step 3: Convert indices to file paths
    for (int index : selectedIndices) {
        if (index >= 0 && index < static_cast<int>(framePaths.size())) {
            result.selectedSlides.push_back(framePaths[index]);
        }
    }

    result.totalFramesProcessed = static_cast<int>(framePaths.size());
    result.processingTimeSeconds = timer.elapsed() / 1000.0;

    return result;
}

SlideDetectionResult SlideDetector::detectSlidesFromFrames(const std::vector<cv::Mat>& frames,
                                                         double ssimThreshold,
                                                         int verificationCount)
{
    SlideDetectionResult result;
    QElapsedTimer timer;
    timer.start();

    if (frames.empty()) {
        result.processingTimeSeconds = timer.elapsed() / 1000.0;
        return result;
    }

    // Step 1: Calculate SSIM scores for all adjacent frame pairs (with default downsampling)
    result.ssimScores = calculateSSIMScoresFromFrames(frames, true, 480, 270);

    // Step 2: Extract slides using the two-stage algorithm
    result.selectedSlideIndices = extractSlidesFromCount(static_cast<int>(frames.size()),
                                                       result.ssimScores,
                                                       ssimThreshold,
                                                       verificationCount);

    result.totalFramesProcessed = static_cast<int>(frames.size());
    result.processingTimeSeconds = timer.elapsed() / 1000.0;

    return result;
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
        result.processingTimeSeconds = timer.elapsed() / 1000.0;
        return result;
    }

    // Step 1: Calculate SSIM scores for all adjacent frame pairs with configurable downsampling
    result.ssimScores = calculateSSIMScoresFromFrames(frames, enableDownsampling, downsampleWidth, downsampleHeight);

    // Step 2: Extract slides using the two-stage algorithm
    result.selectedSlideIndices = extractSlidesFromCount(static_cast<int>(frames.size()),
                                                       result.ssimScores,
                                                       ssimThreshold,
                                                       verificationCount);

    result.totalFramesProcessed = static_cast<int>(frames.size());
    result.processingTimeSeconds = timer.elapsed() / 1000.0;

    return result;
}

SlideDetectionResult SlideDetector::detectSlidesFromChunk(const std::vector<cv::Mat>& newFrames,
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

    if (newFrames.empty()) {
        result.processingTimeSeconds = timer.elapsed() / 1000.0;
        return result;
    }

    // Step 1: Build working frames using single-frame overlap mechanism
    std::vector<cv::Mat> workingFrames;
    int startIndex = 0;

    if (state.isFirstChunk()) {
        // First chunk: use newFrames directly
        workingFrames = newFrames;
        startIndex = 0;
    } else {
        // Subsequent chunks: prepend lastFrame to newFrames
        workingFrames.reserve(newFrames.size() + 1);
        workingFrames.push_back(state.getLastFrameView());
        workingFrames.insert(workingFrames.end(), newFrames.begin(), newFrames.end());
        startIndex = 0; // Always start from comparing lastFrame vs newFrames[0]
    }

    // Step 2: Calculate SSIM scores for all adjacent frame pairs
    result.ssimScores = calculateSSIMScoresFromFrames(workingFrames, enableDownsampling, downsampleWidth, downsampleHeight);

    // Step 3: Handle first frame (only for the very first chunk)
    if (state.isFirstChunk() && state.savedSlideIndices.empty() && !workingFrames.empty()) {
        // The first frame is saved by default
        state.savedSlideIndices.push_back(state.globalFrameOffset);
        state.lastStableIndex = state.globalFrameOffset;
    }

    // Step 4: Main slide detection loop with verification state continuation
    int i = startIndex;
    while (i < static_cast<int>(result.ssimScores.size())) {
        emit slideDetectionProgress(i, static_cast<int>(result.ssimScores.size()));

        if (result.ssimScores[i] < ssimThreshold) {
            // Change detected: frame[i] and frame[i+1] are different
            int potentialSlideLocalIndex = i + 1;
            int potentialSlideGlobalIndex;

            // Convert local index to global index using the simplified formula from PLAN.md
            if (state.isFirstChunk()) {
                potentialSlideGlobalIndex = state.globalFrameOffset + potentialSlideLocalIndex;
            } else {
                // For subsequent chunks: workingFrames = [lastFrame] + newFrames
                // localIndex 0 corresponds to lastFrame (globalIndex = state.lastFrameGlobalIndex)
                // localIndex 1+ corresponds to newFrames[localIndex-1]
                if (potentialSlideLocalIndex == 0) {
                    potentialSlideGlobalIndex = state.lastFrameGlobalIndex;
                } else {
                    potentialSlideGlobalIndex = state.globalFrameOffset + (potentialSlideLocalIndex - 1);
                }
            }

            bool isStable = true;
            int verificationFailedAt = -1;
            int currentVerificationCount = verificationCount;

            // Handle verification state continuation from previous chunk
            // Only continue verification if we're at the chunk boundary (i == 0) and have ongoing verification
            if (i == 0 && !state.isFirstChunk() && state.lastFrameVerificationState != VerificationState::NONE) {
                // Continue verification from previous chunk
                switch (state.lastFrameVerificationState) {
                    case VerificationState::VERIFICATION_1OF3:
                        currentVerificationCount = verificationCount - 1;
                        break;
                    case VerificationState::VERIFICATION_2OF3:
                        currentVerificationCount = verificationCount - 2;
                        break;
                    case VerificationState::VERIFICATION_3OF3:
                        // This should not happen as VERIFICATION_3OF3 means stable
                        currentVerificationCount = verificationCount;
                        break;
                    default:
                        currentVerificationCount = verificationCount;
                        break;
                }
            }

            // Look ahead and check the next currentVerificationCount - 1 scores to verify stability
            for (int j = 0; j < currentVerificationCount - 1; ++j) {
                int checkIndex = potentialSlideLocalIndex + j;

                if (checkIndex >= static_cast<int>(result.ssimScores.size())) {
                    // Reached end of sequence, cannot complete verification
                    isStable = false;
                    verificationFailedAt = checkIndex;
                    break;
                }

                if (result.ssimScores[checkIndex] < ssimThreshold) {
                    // Instability found during verification
                    isStable = false;
                    verificationFailedAt = checkIndex;
                    break;
                }
            }

            if (isStable) {
                // Verification successful
                int newStableLocalIndex = potentialSlideLocalIndex + currentVerificationCount - 1;
                int newStableGlobalIndex;

                // Convert local index to global index using the simplified formula from PLAN.md
                if (state.isFirstChunk()) {
                    newStableGlobalIndex = state.globalFrameOffset + newStableLocalIndex;
                } else {
                    // For subsequent chunks: workingFrames = [lastFrame] + newFrames
                    // localIndex 0 corresponds to lastFrame (globalIndex = state.lastFrameGlobalIndex)
                    // localIndex 1+ corresponds to newFrames[localIndex-1]
                    if (newStableLocalIndex == 0) {
                        newStableGlobalIndex = state.lastFrameGlobalIndex;
                    } else {
                        newStableGlobalIndex = state.globalFrameOffset + (newStableLocalIndex - 1);
                    }
                }

                state.savedSlideIndices.push_back(newStableGlobalIndex);
                state.lastStableIndex = newStableGlobalIndex;
                i = newStableLocalIndex; // Jump past the new stable frame
            } else {
                // Verification failed
                if (verificationFailedAt == -1) {
                    // Verification reached the end of sequence before completion
                    i = static_cast<int>(result.ssimScores.size()); // End the main loop
                } else {
                    // Restart detection from the point of instability
                    i = verificationFailedAt;
                }
            }
        } else {
            // No change detected, continue to the next frame
            i++;
        }
    }

    // Step 5: Handle end-of-sequence logic if this is the last chunk
    if (isLastChunk) {
        int totalFrameCount = state.globalFrameOffset + static_cast<int>(newFrames.size());

        // For end-of-sequence handling, we need to construct a global view
        // Since we only have local SSIM scores, we'll use a simplified approach
        // that works with the available data
        if (state.lastStableIndex == totalFrameCount - 2) {
            // The second to last frame was a stable frame
            // Save the last frame regardless of similarity
            state.savedSlideIndices.push_back(totalFrameCount - 1);
        } else if (state.lastStableIndex == totalFrameCount - 3) {
            // The third to last frame was a stable frame
            // Check if the last two frames are the same (if we have the score)
            int lastScoreIndex = static_cast<int>(result.ssimScores.size()) - 1;
            if (lastScoreIndex >= 0 && result.ssimScores[lastScoreIndex] >= ssimThreshold) {
                // Save if they are the same
                state.savedSlideIndices.push_back(totalFrameCount - 1);
            }
        }

        // Remove duplicates and sort
        std::sort(state.savedSlideIndices.begin(), state.savedSlideIndices.end());
        state.savedSlideIndices.erase(std::unique(state.savedSlideIndices.begin(), state.savedSlideIndices.end()),
                                    state.savedSlideIndices.end());
    }

    // Step 6: Update state for next chunk
    if (!newFrames.empty()) {
        state.setLastFrame(newFrames.back()); // Store a copy using FrameBuffer
        state.lastFrameGlobalIndex = state.globalFrameOffset + static_cast<int>(newFrames.size()) - 1;
    }

    // Update verification state based on current processing position
    // Track verification state for cross-chunk continuity
    updateVerificationStateAtChunkEnd(state, i, static_cast<int>(result.ssimScores.size()), verificationCount);

    // Step 7: Prepare result for this chunk
    // CRITICAL FIX: Return global indices for ProcessingThread to handle conversion
    std::vector<int> chunkSlideIndices;
    int chunkStartGlobal = state.globalFrameOffset;
    int chunkEndGlobal = state.globalFrameOffset + static_cast<int>(newFrames.size()) - 1;

    // Find slides that were detected in this chunk (based on global indices)
    for (int globalIndex : state.savedSlideIndices) {
        if (globalIndex >= chunkStartGlobal && globalIndex <= chunkEndGlobal) {
            // Return global indices - ProcessingThread will convert to local for frame access
            chunkSlideIndices.push_back(globalIndex);
        }
    }

    result.selectedSlideIndices = chunkSlideIndices;
    result.totalFramesProcessed = static_cast<int>(newFrames.size());
    result.processingTimeSeconds = timer.elapsed() / 1000.0;

    return result;
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
        task.enableDownsampling = true;  // Enable downsampling by default
        task.downsampleWidth = 480;
        task.downsampleHeight = 270;
        tasks.push_back(task);
    }

    // Connect progress signal from SSIMCalculator
    connect(&m_ssimCalculator, &SSIMCalculator::calculationProgress,
            this, &SlideDetector::ssimCalculationProgress);

    // Calculate SSIM scores using multi-threading
    std::vector<SSIMResult> results = m_ssimCalculator.calculateMultiThreadedSSIM(tasks);

    // Disconnect progress signal
    disconnect(&m_ssimCalculator, &SSIMCalculator::calculationProgress,
               this, &SlideDetector::ssimCalculationProgress);

    // Extract scores in correct order (results are already ordered by index)
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
    std::vector<double> scores;

    if (frames.size() < 2) {
        return scores;
    }

    // Prepare tasks for multi-threaded SSIM calculation
    std::vector<SSIMTask> tasks;
    tasks.reserve(frames.size() - 1);

    for (size_t i = 0; i < frames.size() - 1; ++i) {
        SSIMTask task;
        task.index = static_cast<int>(i);
        task.img1 = frames[i];
        task.img2 = frames[i + 1];
        task.useMatInput = true;  // Flag to indicate we're using Mat input
        task.enableDownsampling = enableDownsampling;
        task.downsampleWidth = downsampleWidth;
        task.downsampleHeight = downsampleHeight;
        tasks.push_back(task);
    }

    // Connect progress signal from SSIMCalculator
    connect(&m_ssimCalculator, &SSIMCalculator::calculationProgress,
            this, &SlideDetector::ssimCalculationProgress);

    // Calculate SSIM scores using multi-threading
    std::vector<SSIMResult> results = m_ssimCalculator.calculateMultiThreadedSSIM(tasks);

    // Disconnect progress signal
    disconnect(&m_ssimCalculator, &SSIMCalculator::calculationProgress,
               this, &SlideDetector::ssimCalculationProgress);

    // Extract scores in correct order (results are already ordered by index)
    scores.reserve(results.size());
    for (const auto& result : results) {
        scores.push_back(result.score);
    }

    return scores;
}

std::vector<int> SlideDetector::extractSlides(const std::vector<std::string>& framePaths,
                                            const std::vector<double>& scores,
                                            double ssimThreshold,
                                            int verificationCount)
{
    std::vector<int> savedSlideIndices;

    if (framePaths.empty()) {
        return savedSlideIndices;
    }

    // The first frame is saved by default
    savedSlideIndices.push_back(0);
    int lastStableIndex = 0;

    int i = 0;
    while (i < static_cast<int>(scores.size())) {
        emit slideDetectionProgress(i, static_cast<int>(scores.size()));

        if (scores[i] < ssimThreshold) {
            // Change detected: frame[i] and frame[i+1] are different
            int potentialSlideIndex = i + 1;
            bool isStable = true;
            int verificationFailedAt = -1;

            // Look ahead and check the next VERIFICATION_COUNT - 1 scores to verify stability
            for (int j = 0; j < verificationCount - 1; ++j) {
                int checkIndex = potentialSlideIndex + j;

                if (checkIndex >= static_cast<int>(scores.size())) {
                    // Reached end of sequence, cannot complete verification
                    isStable = false;
                    verificationFailedAt = checkIndex;
                    break;
                }

                if (scores[checkIndex] < ssimThreshold) {
                    // Instability found during verification
                    isStable = false;
                    verificationFailedAt = checkIndex;
                    break;
                }
            }

            if (isStable) {
                // Verification successful - corresponds to logic (iii)
                int newStableIndex = potentialSlideIndex + verificationCount - 1;
                savedSlideIndices.push_back(newStableIndex);
                lastStableIndex = newStableIndex;
                i = newStableIndex; // Jump the index directly past the new stable frame
            } else {
                // Verification failed - corresponds to logic (i) and (ii)
                if (verificationFailedAt == -1) {
                    // Verification reached the end of sequence before completion
                    i = static_cast<int>(scores.size()); // End the main loop
                } else {
                    // Restart detection from the point of instability
                    i = verificationFailedAt;
                }
            }
        } else {
            // No change detected, continue to the next frame
            i++;
        }
    }

    // Handle end-of-sequence logic
    handleEndOfSequence(framePaths, scores, lastStableIndex, savedSlideIndices, ssimThreshold);

    return savedSlideIndices;
}

void SlideDetector::handleEndOfSequence(const std::vector<std::string>& framePaths,
                                      const std::vector<double>& scores,
                                      int lastStableIndex,
                                      std::vector<int>& savedSlideIndices,
                                      double ssimThreshold)
{
    int N = static_cast<int>(framePaths.size());

    if (lastStableIndex == N - 2) {
        // The second to last frame was a stable frame
        // Save the last frame regardless of similarity
        savedSlideIndices.push_back(N - 1);
    } else if (lastStableIndex == N - 3) {
        // The third to last frame was a stable frame
        if (N - 2 < static_cast<int>(scores.size()) && scores[N - 2] >= ssimThreshold) {
            // Check if the last two frames are the same
            // Save if they are the same
            savedSlideIndices.push_back(N - 1);
        }
    }

    // Remove duplicates and sort
    std::sort(savedSlideIndices.begin(), savedSlideIndices.end());
    savedSlideIndices.erase(std::unique(savedSlideIndices.begin(), savedSlideIndices.end()), savedSlideIndices.end());
}

std::vector<int> SlideDetector::extractSlidesFromCount(int frameCount,
                                                     const std::vector<double>& scores,
                                                     double ssimThreshold,
                                                     int verificationCount)
{
    std::vector<int> savedSlideIndices;

    if (frameCount <= 0) {
        return savedSlideIndices;
    }

    // The first frame is saved by default
    savedSlideIndices.push_back(0);
    int lastStableIndex = 0;

    int i = 0;
    while (i < static_cast<int>(scores.size())) {
        emit slideDetectionProgress(i, static_cast<int>(scores.size()));

        if (scores[i] < ssimThreshold) {
            // Change detected: frame[i] and frame[i+1] are different
            int potentialSlideIndex = i + 1;
            bool isStable = true;
            int verificationFailedAt = -1;

            // Look ahead and check the next VERIFICATION_COUNT - 1 scores to verify stability
            for (int j = 0; j < verificationCount - 1; ++j) {
                int checkIndex = potentialSlideIndex + j;

                if (checkIndex >= static_cast<int>(scores.size())) {
                    // Reached end of sequence, cannot complete verification
                    isStable = false;
                    verificationFailedAt = checkIndex;
                    break;
                }

                if (scores[checkIndex] < ssimThreshold) {
                    // Instability found during verification
                    isStable = false;
                    verificationFailedAt = checkIndex;
                    break;
                }
            }

            if (isStable) {
                // Verification successful - corresponds to logic (iii)
                int newStableIndex = potentialSlideIndex + verificationCount - 1;
                savedSlideIndices.push_back(newStableIndex);
                lastStableIndex = newStableIndex;
                i = newStableIndex; // Jump the index directly past the new stable frame
            } else {
                // Verification failed - corresponds to logic (i) and (ii)
                if (verificationFailedAt == -1) {
                    // Verification reached the end of sequence before completion
                    i = static_cast<int>(scores.size()); // End the main loop
                } else {
                    // Restart detection from the point of instability
                    i = verificationFailedAt;
                }
            }
        } else {
            // No change detected, continue to the next frame
            i++;
        }
    }

    // Handle end-of-sequence logic (adapted for frame count)
    handleEndOfSequenceFromCount(frameCount, scores, lastStableIndex, savedSlideIndices, ssimThreshold);

    return savedSlideIndices;
}

void SlideDetector::handleEndOfSequenceFromCount(int frameCount,
                                                const std::vector<double>& scores,
                                                int lastStableIndex,
                                                std::vector<int>& savedSlideIndices,
                                                double ssimThreshold)
{
    int N = frameCount;

    if (lastStableIndex == N - 2) {
        // The second to last frame was a stable frame
        // Save the last frame regardless of similarity
        savedSlideIndices.push_back(N - 1);
    } else if (lastStableIndex == N - 3) {
        // The third to last frame was a stable frame
        if (N - 2 < static_cast<int>(scores.size()) && scores[N - 2] >= ssimThreshold) {
            // Check if the last two frames are the same
            // Save if they are the same
            savedSlideIndices.push_back(N - 1);
        }
    }

    // Remove duplicates and sort
    std::sort(savedSlideIndices.begin(), savedSlideIndices.end());
    savedSlideIndices.erase(std::unique(savedSlideIndices.begin(), savedSlideIndices.end()), savedSlideIndices.end());
}

void SlideDetector::updateVerificationState(ProcessingState& state,
                                          int currentLocalIndex,
                                          int verificationProgress,
                                          int verificationCount)
{
    // Update verification state based on progress
    if (verificationProgress == 0) {
        state.lastFrameVerificationState = VerificationState::NONE;
    } else if (verificationProgress == 1 && verificationCount >= 3) {
        state.lastFrameVerificationState = VerificationState::VERIFICATION_1OF3;
    } else if (verificationProgress == 2 && verificationCount >= 3) {
        state.lastFrameVerificationState = VerificationState::VERIFICATION_2OF3;
    } else if (verificationProgress >= 3) {
        state.lastFrameVerificationState = VerificationState::VERIFICATION_3OF3;
    } else {
        state.lastFrameVerificationState = VerificationState::NONE;
    }

    // Update verification start index if starting new verification
    if (verificationProgress == 1) {
        // Convert local index to global index
        if (state.isFirstChunk()) {
            state.verificationStartIndex = state.globalFrameOffset + currentLocalIndex;
        } else {
            if (currentLocalIndex == 0) {
                state.verificationStartIndex = state.lastFrameGlobalIndex;
            } else {
                state.verificationStartIndex = state.globalFrameOffset + (currentLocalIndex - 1);
            }
        }
    }
}

void SlideDetector::updateVerificationStateAtChunkEnd(ProcessingState& state,
                                                    int finalProcessingIndex,
                                                    int totalScores,
                                                    int verificationCount)
{
    // Determine verification state based on where processing ended
    // This is critical for cross-chunk verification continuity

    if (finalProcessingIndex >= totalScores) {
        // Processing completed normally - no ongoing verification
        state.lastFrameVerificationState = VerificationState::NONE;
        state.verificationStartIndex = -1;
    } else {
        // Processing may have ended mid-verification due to chunk boundary
        // We need to analyze the context to determine the verification state

        // For now, implement a simplified approach:
        // If we ended before completing all scores, assume no ongoing verification
        // A more sophisticated implementation would track the exact verification state
        state.lastFrameVerificationState = VerificationState::NONE;
        state.verificationStartIndex = -1;
    }
}

SlideDetectionResult SlideDetector::detectSlidesFromFrameBufferChunk(const std::vector<FrameBuffer>& frameBuffers,
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

    if (frameBuffers.empty()) {
        result.processingTimeSeconds = timer.elapsed() / 1000.0;
        return result;
    }

    // Step 1: Build working frames using zero-copy Mat views
    std::vector<cv::Mat> workingFrames;
    int startIndex = 0;

    if (state.isFirstChunk()) {
        // First chunk: use frameBuffers directly (zero-copy views)
        workingFrames.reserve(frameBuffers.size());
        for (const auto& buffer : frameBuffers) {
            if (buffer.isValid()) {
                workingFrames.push_back(buffer.getConstMatView());
            }
        }
        startIndex = 0;
    } else {
        // Subsequent chunks: prepend lastFrame to frameBuffers
        workingFrames.reserve(frameBuffers.size() + 1);
        workingFrames.push_back(state.getLastFrameView());
        for (const auto& buffer : frameBuffers) {
            if (buffer.isValid()) {
                workingFrames.push_back(buffer.getConstMatView());
            }
        }
        startIndex = 0; // Always start from comparing lastFrame vs frameBuffers[0]
    }

    // Step 2: Calculate SSIM scores using optimized method
    result.ssimScores = calculateSSIMScoresFromFrameBuffers(frameBuffers, enableDownsampling, downsampleWidth, downsampleHeight);

    // Step 3: Handle first frame (only for the very first chunk)
    if (state.isFirstChunk() && state.savedSlideIndices.empty() && !workingFrames.empty()) {
        // The first frame is saved by default
        state.savedSlideIndices.push_back(state.globalFrameOffset);
        state.lastStableIndex = state.globalFrameOffset;
    }

    // Step 4: Main slide detection loop with verification state continuation
    int i = startIndex;
    while (i < static_cast<int>(result.ssimScores.size())) {
        emit slideDetectionProgress(i, static_cast<int>(result.ssimScores.size()));

        if (result.ssimScores[i] < ssimThreshold) {
            // Potential slide change detected
            int candidateIndex = state.globalFrameOffset + i + 1;

            // Check if we can verify stability (need verificationCount consecutive frames)
            bool canVerify = (i + verificationCount) < static_cast<int>(result.ssimScores.size());

            if (canVerify) {
                // Verify stability by checking the next verificationCount frames
                bool isStable = true;
                for (int j = 1; j <= verificationCount; j++) {
                    if (result.ssimScores[i + j] < ssimThreshold) {
                        isStable = false;
                        break;
                    }
                }

                if (isStable) {
                    // Stable slide found
                    state.savedSlideIndices.push_back(candidateIndex);
                    state.lastStableIndex = candidateIndex;

                    // Skip the verification frames
                    i += verificationCount + 1;
                } else {
                    i++;
                }
            } else {
                // Cannot verify within this chunk - handle based on chunk type
                if (isLastChunk) {
                    // Last chunk: save the candidate if it's different from the last stable
                    if (candidateIndex != state.lastStableIndex) {
                        state.savedSlideIndices.push_back(candidateIndex);
                        state.lastStableIndex = candidateIndex;
                    }
                    i++;
                } else {
                    // Not last chunk: defer verification to next chunk
                    updateVerificationState(state, i, 0, verificationCount);
                    break;
                }
            }
        } else {
            i++;
        }
    }

    // Step 5: Handle end-of-sequence for last chunk
    if (isLastChunk) {
        int lastFrameIndex = state.globalFrameOffset + static_cast<int>(frameBuffers.size()) - 1;
        if (lastFrameIndex != state.lastStableIndex && !frameBuffers.empty()) {
            state.savedSlideIndices.push_back(lastFrameIndex);
        }

        // Remove duplicates and sort
        std::sort(state.savedSlideIndices.begin(), state.savedSlideIndices.end());
        state.savedSlideIndices.erase(std::unique(state.savedSlideIndices.begin(), state.savedSlideIndices.end()),
                                    state.savedSlideIndices.end());
    }

    // Step 6: Update state for next chunk using zero-copy FrameBuffer
    if (!frameBuffers.empty() && frameBuffers.back().isValid()) {
        // Move the last FrameBuffer to state (zero-copy)
        state.setLastFrame(FrameBuffer::fromMat(frameBuffers.back().getConstMatView()));
        state.lastFrameGlobalIndex = state.globalFrameOffset + static_cast<int>(frameBuffers.size()) - 1;
    }

    // Update verification state based on current processing position
    updateVerificationStateAtChunkEnd(state, i, static_cast<int>(result.ssimScores.size()), verificationCount);

    // Update global frame offset for next chunk
    state.updateGlobalOffset(static_cast<int>(frameBuffers.size()));

    result.selectedSlideIndices = state.savedSlideIndices;
    result.totalFramesProcessed = static_cast<int>(frameBuffers.size());
    result.processingTimeSeconds = timer.elapsed() / 1000.0;

    return result;
}

std::vector<double> SlideDetector::calculateSSIMScoresFromFrameBuffers(const std::vector<FrameBuffer>& frameBuffers,
                                                                      bool enableDownsampling,
                                                                      int downsampleWidth,
                                                                      int downsampleHeight)
{
    std::vector<double> scores;
    if (frameBuffers.size() < 2) {
        return scores;
    }

    scores.reserve(frameBuffers.size() - 1);

    // Use memory pool for efficient Mat management
    auto memoryPool = std::make_shared<MatMemoryPool>(50);

    for (size_t i = 0; i < frameBuffers.size() - 1; ++i) {
        emit ssimCalculationProgress(static_cast<int>(i), static_cast<int>(frameBuffers.size() - 1));

        if (!frameBuffers[i].isValid() || !frameBuffers[i + 1].isValid()) {
            scores.push_back(0.0); // Invalid frames get 0 similarity
            continue;
        }

        // Get zero-copy Mat views
        cv::Mat frame1 = frameBuffers[i].getConstMatView();
        cv::Mat frame2 = frameBuffers[i + 1].getConstMatView();

        if (frame1.empty() || frame2.empty()) {
            scores.push_back(0.0);
            continue;
        }

        // Calculate SSIM with memory pool optimization
        double ssim = 0.0;
        if (enableDownsampling) {
            // Use pooled Mats for downsampling
            auto pooledMat1 = PooledMat(memoryPool->acquireWorkBuffer(downsampleWidth, downsampleHeight, frame1.type()), memoryPool.get(), false);
            auto pooledMat2 = PooledMat(memoryPool->acquireWorkBuffer(downsampleWidth, downsampleHeight, frame2.type()), memoryPool.get(), false);

            cv::resize(frame1, pooledMat1.get(), cv::Size(downsampleWidth, downsampleHeight));
            cv::resize(frame2, pooledMat2.get(), cv::Size(downsampleWidth, downsampleHeight));

            ssim = m_ssimCalculator.calculateGlobalSSIM(pooledMat1.get(), pooledMat2.get());
        } else {
            ssim = m_ssimCalculator.calculateGlobalSSIM(frame1, frame2);
        }

        scores.push_back(ssim);
    }

    return scores;
}