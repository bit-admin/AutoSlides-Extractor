#!/usr/bin/env python3
"""Process contract test. Run with --full PATH and/or --helper PATH; needs ffmpeg/libx264."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import signal
import struct
import subprocess
import sys
import tempfile
import time


TITLE = "讲座 room__c70101s850101"
FILENAMES = [f"Slide_{TITLE}_{i:03}.png" for i in range(1, 4)]
PNG_MAGIC = b"\x89PNG\r\n\x1a\n"


def check(ok, message):
    if not ok:
        raise AssertionError(message)


def ffmpeg(*args):
    return subprocess.run(["ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", *args],
                          stdout=subprocess.PIPE, check=True, timeout=60).stdout


def fixture(root):
    # 8 I-frames per color. The changes are at 4 and 8 media seconds;
    # each candidate is confirmed on its third stable sample, one second later.
    video = root / f"screen_{TITLE}.mp4"
    ffmpeg("-f", "lavfi", "-i", "color=black:s=320x180:r=2:d=4[a];"
           "color=red:s=320x180:r=2:d=4[b];color=white:s=320x180:r=2:d=4[c];"
           "[a][b][c]concat=n=3:v=1:a=0", "-c:v", "libx264", "-preset", "ultrafast",
           "-g", "1", "-pix_fmt", "yuv420p", "-movflags", "+faststart", video)
    return video


def command(exe, video, output, *, timeline=False, chunk=100, downsample=True):
    args = [exe, "--video", video, "--output", output, "--json", "--compatible",
            "--ssim-threshold", "0.9987", "--enable-downsampling", str(downsample).lower(),
            "--downsample-width", "480", "--downsample-height", "270", "--chunk-size", str(chunk)]
    return args + (["--write-timeline"] if timeline else [])


def events(raw, stderr=False):
    records = []
    for line in raw.splitlines():
        # FFmpeg and Qt can diagnose bad media on stderr. Stdout belongs
        # entirely to the protocol, including on failure and cancellation.
        if stderr and not line.lstrip().startswith(b"{"):
            continue
        record = json.loads(line.decode("utf-8"))
        check(record["v"] == 1 and record["ts"] and record["event"], record)
        records.append(record)
    return records


def run(args, code):
    result = subprocess.run(args, stdin=subprocess.DEVNULL, capture_output=True, timeout=30)
    check(result.returncode == code, f"{args}\nexpected {code}, got {result.returncode}"
          f"\n{result.stdout[-2000:]!r}\n{result.stderr[-2000:]!r}")
    return events(result.stdout), events(result.stderr, stderr=True)


def failure(args, code, category):
    out, err = run(args, code)
    check(not any(r["event"] in ("slides_extracted", "done", "cancelled", "error") for r in out), out)
    check(len(err) == 1 and err[0]["event"] == "error" and err[0]["category"] == category
          and err[0]["exitCode"] == code and err[0]["message"], err)
    return out, err[0]


def pixels(path, color):
    png = path.read_bytes()
    check(png.startswith(PNG_MAGIC) and png[12:16] == b"IHDR" and
          struct.unpack(">IIBB", png[16:26]) == (320, 180, 8, 2), path)  # 8-bit truecolor
    rgb = ffmpeg("-i", path, "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "rgb24", "-")
    check(len(rgb) == 320 * 180 * 3, path)
    # Small color conversion differences between decoders are fine for the
    # fixture oracle; the A/B comparison uses every decoded pixel, exactly.
    check(all(abs(a - b) <= 5 for a, b in zip(rgb[:3], color)), (path, rgb[:3]))
    return hashlib.sha256(rgb).hexdigest()


def success(exe, video, output, *, timeline, downsample, chunk):
    out, err = run(command(exe, video, output, timeline=timeline, chunk=chunk, downsample=downsample), 0)
    check(not err, err)
    check([r["event"] for r in out if r["event"] != "frame_progress"] ==
          ["start", "info", "slides_extracted", "done"], out)
    start, info, extracted, done = out[0], out[1], out[-2], out[-1]
    check(start["schemaVersion"] == 1 and start["appVersion"] and start["pid"] > 0
          and start["compatible"] is True and start["phases"] == [] and start["ssimThreshold"] == 0.9987
          and Path(start["video"]) == video and Path(start["output"]) == output, start)
    check(info["kind"] == "video" and info["videoWidth"] == 320 and info["videoHeight"] == 180
          and info["durationSec"] == 12 and info["frameRate"] == 2 and info["iFrameIntervalSec"] == 0.5
          and info["decoder"] and info["message"] and isinstance(info["screenRecording"], bool), info)
    progress = out[2:-2]
    check(progress and progress[-1]["percent"] == 100 and
          all(p["event"] == "frame_progress" and p["videoIndex"] == 0 and 0 <= p["percent"] <= 100
              for p in progress), progress)
    slides = output / f"slides_{TITLE}"
    check(Path(extracted["slidesDir"]) == Path(done["slidesDir"]) == slides
          and extracted["count"] == done["slideCount"] == 3 and done["exitCode"] == 0, done)
    check(list(output.iterdir()) == [slides], output)
    expected = FILENAMES + (["timeline.json"] if timeline else [])
    check(sorted(p.name for p in slides.iterdir()) == sorted(expected), slides)
    digests = [pixels(slides / name, color) for name, color in
               zip(FILENAMES, [(0, 0, 0), (255, 0, 0), (255, 255, 255)])]
    if timeline:
        data = json.loads((slides / "timeline.json").read_text(encoding="utf-8"))
        check(data["version"] == 1 and data["extractor"] == "qt" and data["createdAt"] and data["updatedAt"]
              and Path(data["source"]["path"]) == video and data["source"]["duration"] == 12
              and data["source"]["startTime"] == 0, data)
        captures = data["events"]
        check([(e["initialFile"], e["changeAt"], e["confirmedAt"]) for e in captures] ==
              list(zip(FILENAMES, [0, 4, 8], [0, 5, 9])), captures)
        resolutions = {e["id"]: {"state": "canonical", "file": e["initialFile"]} for e in captures}
        check(len(resolutions) == 3 and data["resolutions"] == resolutions, data)
    return start["appVersion"], digests


def helper_failures(exe, video, root):
    args = command(exe, video, root / "bad flags")
    for flag in ("--phash-redundant", "--ml-classify", "--export-pdf", "--export-video", "--not-a-flag"):
        failure([*args, flag], 2, "bad_args")
    failure([arg for arg in args if arg != "--compatible"], 2, "bad_args")

    # Directories in place of files fail even under root (unlike chmod 000).
    # Fail after a prior successful slide, while the producer can be waiting
    # on a full queue. Neither a hang nor a fictitious slide count is OK.
    for obstacle in (FILENAMES[1], "timeline.json"):
        output = root / ("blocked " + obstacle)
        slides = output / f"slides_{TITLE}"
        (slides / obstacle).mkdir(parents=True)
        failure(command(exe, video, output, timeline=True, chunk=1), 4, "processing_failed")
        if obstacle != "timeline.json":
            check((slides / FILENAMES[0]).is_file(), slides)

    # A faststart MP4 with its index and stream metadata but no samples:
    # analysis opens successfully, extraction yields zero, consumer must wake.
    content = video.read_bytes()
    empty = root / "indexed but empty.mp4"
    empty.write_bytes(content[:content.index(b"mdat") + 4])
    out, error = failure(command(exe, empty, root / "zero frames"), 4, "processing_failed")
    check(any(r["event"] == "info" and r["kind"] == "video" for r in out)
          and "No frames extracted" in error["message"], (out, error))


def cancel(exe, video, output, sig):
    first = output / "slides_long" / "Slide_long_001.png"
    args = command(exe, video, output, timeline=True, chunk=3)
    with subprocess.Popen(args, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE) as proc:
        # Drain both real pipes continuously. Waiting for progress alone would
        # also pass if nothing had been written or extraction was already done.
        with ThreadPoolExecutor(1) as pool:
            io = pool.submit(proc.communicate)
            try:
                deadline = time.monotonic() + 30
                while not io.done() and time.monotonic() < deadline:
                    if first.is_file() and first.read_bytes().endswith(b"IEND\xaeB`\x82"):
                        break
                    time.sleep(0.005)
                else:
                    raise AssertionError(f"no completed PNG before cancellation: {first}")
                proc.send_signal(sig)
                stdout, stderr = io.result(timeout=15)
            finally:
                if proc.poll() is None:
                    proc.kill()
    code = 128 + int(sig)
    out = events(stdout)
    check(proc.returncode == code and not events(stderr, stderr=True), (proc.returncode, stderr[-2000:]))
    check([r["event"] for r in out if r["event"] != "frame_progress"] == ["start", "info", "cancelled"]
          and out[-1]["signal"] == sig.name and out[-1]["exitCode"] == code, out[-5:])
    # The partial folder belongs to Electron. The worker must neither delete it
    # nor finish the whole 384-slide fixture before acknowledging cancellation.
    check(first.is_file() and 1 <= len(list(first.parent.glob("*.png"))) < 384, first.parent)
    pixels(first, (0, 0, 0))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--full", type=Path, help="full AutoSlidesExtractor executable (baseline)")
    parser.add_argument("--helper", type=Path, help="standalone helper executable")
    args = parser.parse_args()
    if not args.full and not args.helper:
        parser.error("supply --full, --helper, or both")
    with tempfile.TemporaryDirectory(prefix="Electron contract 空 ") as tmp:
        root = Path(tmp)
        video = fixture(root)
        long_video = root / "screen_long.mp4"
        if sys.platform != "win32":  # POSIX signals; Windows termination need not emit an event.
            ffmpeg("-stream_loop", "127", "-i", video, "-c", "copy", long_video)
        results = []
        for label, exe in (("full", args.full), ("helper", args.helper)):
            if not exe:
                continue
            # Keep the given path: the Linux desktop entry point is a symlink to
            # AppRun, which must not be bypassed by resolving it.
            exe = exe.absolute()
            check(exe.is_file(), f"not an executable file: {exe}")
            work = root / label
            work.mkdir()
            help_out, err = run([exe, "--help", "--json"], 0)
            check(not err and len(help_out) == 1 and help_out[0]["event"] == "help" and
                  all(flag in help_out[0]["text"] for flag in ("--video", "--output", "--compatible",
                                                               "--json", "--write-timeline")), help_out)
            version_out, err = run([exe, "--version", "--json"], 0)
            check(not err and len(version_out) == 1 and version_out[0]["event"] == "version", version_out)
            # Timeline and downsampling vary independently (plus chunk size), so a
            # difference points at one setting. None of them may change the slides.
            runs = [success(exe, video, work / f"run {i}", timeline=timeline, downsample=downsample, chunk=chunk)
                    for i, (timeline, downsample, chunk) in enumerate(
                        [(True, True, 100), (False, True, 3), (True, False, 1), (False, False, 100)])]
            check(version_out[0]["appVersion"] == runs[0][0], version_out)
            check(all(r == runs[0] for r in runs), f"timeline, downsampling or chunk size changed slides: {runs}")
            results.append(runs[0])

            failure(command(exe, root / "missing.mp4", work / "missing"), 3, "bad_input")
            failure(command(exe, video, video), 3, "bad_input")  # base output is a regular file
            damaged = work / "invalid.mp4"
            damaged.write_bytes(b"not a video")
            failure(command(exe, damaged, work / "invalid"), 4, "processing_failed")
            if label == "helper":
                helper_failures(exe, video, work)
            if sys.platform != "win32":  # POSIX signals; Windows termination need not emit an event.
                for sig in (signal.SIGTERM, signal.SIGINT):
                    cancel(exe, long_video, work / sig.name, sig)
            print(f"{label}: contract passed ({exe})", flush=True)
        if len(results) == 2:
            check(results[0] == results[1], f"A/B mismatch in version or decoded RGB pixels: {results}")
            print("A/B: matching versions, decoded pixels, names, counts and media timings")


if __name__ == "__main__":
    main()
