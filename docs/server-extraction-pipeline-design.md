# Server Extraction Pipeline Design

This document describes how to build a server-side extraction worker that preserves the desktop app's slide-detection behavior while fitting resource-limited machines. It intentionally ignores video download, durable output storage, and serving. The focus is the extraction pipeline itself.

## Scope

In scope:

- Decode and sample presentation videos.
- Detect slide changes using the same SSIM-driven behavior as the Qt app.
- Save full-resolution selected slide images.
- Run optional post-processing after extraction.
- Keep RAM bounded for small workers.

Out of scope:

- Upload/download orchestration.
- API authentication.
- Object storage layout.
- PDF export and Slides Review UI.

## Current Desktop Pipeline

The Qt app currently processes each video through `ProcessingThread::processVideoWithChunks()`:

1. `HardwareDecoder` opens the video, reads metadata, estimates I-frame interval, and chooses an I-frame sampling strategy.
2. A producer thread calls `HardwareDecoder::extractFramesInChunks()`.
3. The producer decodes selected I-frames into full-resolution BGR `cv::Mat` frames and groups them into chunks.
4. A capacity-1 queue passes each chunk to a consumer thread.
5. The consumer calls `SlideDetector::detectSlidesFromChunk()`.
6. `SlideDetector` prepends the previous chunk's last frame, computes adjacent SSIM scores for the whole chunk, applies the two-stage verification algorithm, and returns selected frame indices.
7. The consumer writes selected full-resolution frames as `slide_<video>_<nnn>.jpg` or compatible-mode PNG.
8. Optional post-processing removes pHash duplicates, pHash exclusions, and ML-classified non-slide images.

Important implementation references:

- `src/processingthread.cpp`: chunk orchestration and output writing.
- `src/hardwaredecoder.cpp`: FFmpeg decode, I-frame sampling, and frame conversion.
- `src/slidedetector.cpp`: SSIM chunk processing and verification logic.
- `src/ssimcalculator.cpp`: global SSIM and multithreaded chunk scoring.
- `src/postprocessor.cpp`: pHash and ML cleanup.

## Why The Current Chunk Path Uses GBs Of RAM

The desktop path is chunked, but the chunk still contains full decoded BGR frames.

Approximate frame memory:

| Resolution | BGR frame size | 100-frame chunk |
| --- | ---: | ---: |
| 1920x1080 | 5.9 MiB | 593 MiB |
| 2560x1440 | 10.5 MiB | 1.0 GiB |
| 3840x2160 | 23.7 MiB | 2.3 GiB |

Peak can exceed one chunk because the producer may be filling the next chunk while the consumer is still scoring the previous one. On 4K inputs, two chunks alone can approach 4.6 GiB before decoder buffers, Qt/OpenCV overhead, downsample work buffers, ONNX runtime memory, and output encoding. This matches the observed 3-4 GB behavior.

Lowering `chunkSize` helps, but it is not the best server design. The chunk detector has simplified cross-boundary verification state, so very small chunks can also make boundary behavior fragile. A server worker should avoid full-frame chunks entirely.

## Recommended Server Architecture

Use a probe-first extraction worker:

```text
input video
  -> analyze video metadata and sampling strategy
  -> pass 1: stream sampled frames as small grayscale probes
  -> streaming SSIM detector emits selected sample indices/timestamps
  -> pass 2: decode only selected frames at full resolution
  -> write slide images
  -> optional pHash / ML cleanup
```

This is slower than the Qt chunk path because it may decode the video twice, but it makes memory nearly constant. Long processing time is acceptable for the target server use case.

## Pipeline Stages

### 1. Analyze

Open the video with FFmpeg and collect:

- duration
- frame rate
- width and height
- codec
- average I-frame interval
- screen-recording heuristic
- selected sampling strategy

Use the same sampling strategy as the app:

| I-frame interval | Strategy |
| ---: | --- |
| `>= 4.0s` | use all I-frames, warn about sparse samples |
| `1.6s..1.9s` | use all I-frames |
| `1.0s..1.5s` | skip every other I-frame |
| other | use all I-frames |

For server compatibility, keep these values configurable but default them to desktop behavior.

### 2. Pass 1: Probe Decode

Decode only the sampled frames needed for detection. For each accepted sample:

1. Decode the frame.
2. Produce a detector-size probe, usually `480x270`.
3. Emit `SampleProbe`:

```text
sampleIndex
sourcePts
sourceTimestamp
isKeyFrame
probeGray8
```

The strict low-memory path should avoid a full-resolution BGR `cv::Mat` in pass 1 and use FFmpeg scaling/conversion to create the small probe directly. If exact desktop parity is more important than the lowest possible memory, it is also acceptable to hold one full-resolution frame at a time, run the same OpenCV resize/grayscale steps as the desktop code, and release it immediately. That still avoids the multi-GB chunk problem because only one full frame is resident.

Memory target:

- two 480x270 grayscale probes: about 260 KiB total
- strict low-memory conversion: no full-resolution BGR frame in pass 1
- compatibility-first conversion: one temporary full-resolution frame, released after probe creation
- plus decoder buffers
- no full-frame chunk

### 3. Streaming SSIM

For each adjacent probe pair, calculate the same global SSIM concept used by the app:

- downsampled image size: default `480x270`
- grayscale
- constants: `C1 = 6.5025`, `C2 = 58.5225`
- no extra normalization beyond the desktop formula; scores should normally be near `0..1`
- threshold defaults:
  - Strict: `0.999`
  - Normal: `0.9985`
  - Loose: `0.998`
  - Custom: user supplied

Use one or two CPU threads inside a low-memory worker. Prefer job-level parallelism over per-video SSIM parallelism. The desktop app's multithreaded SSIM is useful on a workstation, but on small servers it increases transient allocations and competes with decoding.

### 4. Streaming Slide Detector

Implement a score-driven detector that is equivalent to the app's two-stage algorithm but does not require a chunk of frames.

Desktop behavior to preserve:

- The first sampled frame is always selected.
- A slide change is detected when `ssim[i] < threshold`.
- The candidate is `frame[i + 1]`.
- The candidate is accepted only after `verificationCount - 1` following scores are stable.
- With the current hardcoded `verificationCount = 3`, a change at score `i` selects `frame[i + 3]` if `ssim[i + 1]` and `ssim[i + 2]` are both `>= threshold`.
- If verification fails because another low SSIM appears, restart detection from that low score.
- At end of stream, apply the app's final-frame rule:
  - if the last stable frame is `N - 2`, also save `N - 1`
  - if the last stable frame is `N - 3` and the last score is stable, also save `N - 1`

Recommended state:

```text
lastStableIndex
selectedIndices
lastScore
pendingCandidateStartScoreIndex
pendingStableScoreCount
totalSampleCount
```

The state machine only needs recent score metadata. It does not need full frames.

### 5. Pass 2: Full-Resolution Materialization

After pass 1 has selected sample indices/timestamps, run a second decode pass and write only selected full-resolution frames.

Recommended behavior:

1. Iterate through the same sampling sequence as pass 1.
2. When the current `sampleIndex` is selected, convert that frame to BGR/RGB at original resolution.
3. Write it with the same naming and quality rules:
   - default: `slide_<videoName>_<nnn>.jpg`
   - JPEG quality from config, default `95`
   - compatible mode, if needed later: `Slide_<videoName>_<nnn>.png`
4. Release the full-resolution frame immediately.

This avoids random seek accuracy issues because pass 2 replays the same sampling sequence. It is slower than seeking directly to selected timestamps, but it is deterministic and simple.

If pass 2 must be faster, an optimized version can seek near selected packet PTS values. That path must verify the decoded sample index or PTS before writing, otherwise non-keyframe seeking can produce off-by-one outputs.

## One-Pass Alternative

If decoding twice is too expensive, use a one-pass candidate spool:

```text
decode sampled frame
  -> create grayscale probe for SSIM
  -> keep or temp-encode only the last verification window
  -> when detector confirms a selected index, write or promote that full frame
```

Two variants:

- Memory ring: keep `verificationCount + 1` full-resolution frames in memory. With 4K BGR and `verificationCount = 3`, this is roughly 95 MiB plus decoder buffers.
- Disk spool: encode recent candidate frames to temporary JPEG/PNG files and keep only paths in memory. This minimizes RAM but adds disk I/O and cleanup complexity.

The two-pass design is still preferred for a small server because it is easier to reason about, resume, and bound.

## Post-Processing On Server

The extraction worker can run post-processing after full-resolution slides are written.

### pHash Duplicate Removal

The current pHash implementation stores one 256-bit hash per slide and compares pairs by Hamming distance. Memory is small:

```text
32 bytes * slide_count
```

The time complexity is `O(slide_count^2)`. For typical presentation slide counts this is acceptable. If server jobs produce thousands of slides, consider a BK-tree or locality-sensitive bucketing over the 256-bit hashes.

### pHash Exclusion

Compare each slide hash against configured exclusion hashes. This is memory-light and can run in the same hash pass.

### ML Classification

Run MobileNetV4 classification one image at a time. Avoid true batch inference on small workers unless the batch size is explicitly capped. ONNX Runtime providers can reserve substantial memory, so CPU provider with one or two intra-op threads is the safest baseline for small instances.

## Resource Profiles

### Low-Memory Profile

Use this for small CPU workers.

```text
pass1Decode: sampled keyframes only
pass1FrameFormat: 480x270 GRAY8
fullFrameRetention: none
materialization: second sequential decode pass
ssimThreads: 1
onnxProvider: CPU
onnxThreads: 1-2
concurrentJobsPerWorker: 1
```

Expected RAM: decoder buffers plus a few MiB for probes and state. During pass 2, add one full-resolution output frame.

### Balanced Profile

Use this when CPU is moderate and memory is still capped.

```text
pass1Decode: sampled keyframes only
pass1FrameFormat: 480x270 GRAY8
materialization: second sequential decode pass or verified timestamp seek
ssimThreads: 2
onnxProvider: CPU or available accelerator
concurrentJobsPerWorker: 1
```

Expected RAM: still low, but ONNX or hardware decode may reserve extra memory.

### Fast Worker Profile

Use this only for larger instances.

```text
pass1Decode: sampled keyframes only
onePassCandidateSpool: memory ring or disk spool
ssimThreads: 2-4
onnxProvider: accelerator if stable
concurrentJobsPerWorker: controlled by memory budget
```

This profile reduces wall time but should still avoid full-frame chunks.

## Manifest And Resume

Even if storage is out of scope, the extraction worker should have a local manifest so jobs can be debugged and resumed.

Suggested manifest fields:

```json
{
  "schemaVersion": 1,
  "video": {
    "duration": 0,
    "width": 0,
    "height": 0,
    "frameRate": 0,
    "codec": "",
    "avgIFrameInterval": 0,
    "samplingStrategy": "UseAllIFrames"
  },
  "config": {
    "ssimThreshold": 0.9985,
    "verificationCount": 3,
    "downsampleWidth": 480,
    "downsampleHeight": 270,
    "jpegQuality": 95
  },
  "samples": [],
  "selected": [],
  "postProcessing": {}
}
```

For large videos, store `samples` and SSIM scores as NDJSON rows or in SQLite rather than one large JSON array.

## Error Handling And Cancellation

The worker should checkpoint after each stage:

1. analyzed
2. probe pass complete
3. materialization complete
4. post-processing complete

Cancellation should interrupt FFmpeg reads, clean temporary candidate spools, and leave already-written final slides intact or mark them as partial in the manifest. Do not rely on process kill for normal cancellation because FFmpeg and ONNX can hold large native allocations until process exit.

## Compatibility Checklist

To match desktop extraction results as closely as possible:

- Use the same I-frame sampling strategy.
- Use the same sample ordering.
- Use the same SSIM formula and threshold.
- Use the same downsample dimensions.
- Use `verificationCount = 3` unless making it intentionally configurable.
- Preserve the same end-of-stream final-frame rule.
- Materialize selected samples from the same sampled sequence, not from arbitrary wall-clock timestamps.
- Run pHash before ML classification if post-processing is enabled.

## Design Decision

Do not port the current chunk-based Qt extraction loop directly to the server. It is optimized for workstation throughput, not strict memory ceilings.

The recommended server v1 is:

1. Probe-only streaming detection pass.
2. Sequential full-resolution materialization pass for selected samples.
3. Optional memory-bounded post-processing.
4. One job per low-memory worker process.

This trades wall time for predictable memory. For resource-limited servers, that is the right tradeoff.
