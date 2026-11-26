# AutoSlidesExtractorHelper

This is the extraction-only executable for an Electron host, not a replacement for the desktop application. Extract the entire archive into its own directory and spawn `AutoSlidesExtractorHelper/AutoSlidesExtractorHelper` (`AutoSlidesExtractorHelper.exe` inside the same directory on Windows) with an argument array. Keep the accompanying libraries and licence notices in place. The name differs from the desktop executable on purpose: the Electron app downloads, verifies, updates and spawns its own copy (see [Releases](#releases)), so it never picks up, or interferes with, an installed desktop app. No Qt plugins, ONNX models, PDF/video export, or post-processing are included.

```sh
AutoSlidesExtractorHelper --help --json
AutoSlidesExtractorHelper --version --json
AutoSlidesExtractorHelper --json --compatible --write-timeline \
  --video '/input/screen_示例课程__c70101s850101.mp4' --output '/output/AutoSlides' \
  --ssim-threshold 0.9987 --enable-downsampling true \
  --downsample-width 480 --downsample-height 270 --chunk-size 100
```

`--output` is the parent directory. Success is exit 0 **and** the `done` event on stdout. Structured errors go to stderr. As in the desktop CLI, `--write-timeline` is opt-in; the helper requires `--compatible` for extraction. It uses the existing extractor settings for options not overridden by the host. Use a fresh output directory for each run; retrying in a partially populated folder does not clean stale slides.

## Releases

A `v<version>` tag builds the helper together with the desktop packages ([Release](../../.github/workflows/release.yml)). The helper shares the app version. Before the draft is created, `tests/electron_contract.py --full … --helper …` runs the shipped desktop package and the helper archive on the same runner. They must produce the same PNG names, decoded pixels, counts and timeline timings. The draft release then carries, next to the six desktop packages:

| Asset | Contents |
| --- | --- |
| `AutoSlidesExtractorHelper-<ver>-macOS-arm64.tar.gz` | macOS 12+ Apple Silicon, ad-hoc signed |
| `AutoSlidesExtractorHelper-<ver>-Windows-x64.zip` | Windows x64, console EXE with app-local MSVC runtime |
| `AutoSlidesExtractorHelper-<ver>-Linux-x86_64.tar.gz` | x86-64 CPU with AVX2, glibc 2.35+ (Ubuntu 22.04 and newer) |
| `AutoSlidesExtractorHelper-manifest.json` | what the Electron app reads (below) |
| `SHA256SUMS.txt` | SHA-256 of every release asset |

The newest **published** manifest is always at
`https://github.com/bit-admin/AutoSlides-Extractor/releases/latest/download/AutoSlidesExtractorHelper-manifest.json`
(drafts and pre-releases are never "latest"). The URLs inside it are versioned and never change:

```json
{
  "schemaVersion": 1,
  "name": "AutoSlidesExtractorHelper",
  "version": "2.2.0",
  "tag": "v2.2.0",
  "cliSchemaVersion": 1,
  "platforms": {
    "darwin-arm64": { "file": "AutoSlidesExtractorHelper-2.2.0-macOS-arm64.tar.gz", "url": "https://github.com/…/releases/download/v2.2.0/…",
                      "size": 0, "sha256": "…", "format": "tar.gz",
                      "executable": "AutoSlidesExtractorHelper/AutoSlidesExtractorHelper" },
    "linux-x64":    { "…": "…" },
    "win32-x64":    { "…": "…", "format": "zip", "executable": "AutoSlidesExtractorHelper/AutoSlidesExtractorHelper.exe" }
  }
}
```

Platform keys are Node's `` `${process.platform}-${process.arch}` ``. `cliSchemaVersion` is the `v` field of the NDJSON events. A suggested host flow is:

1. Fetch the manifest.
2. Compare `version` with the installed copy.
3. Download `url` into a temporary file and check `size` and `sha256`.
4. Unpack it into a new versioned directory, then switch to it.
5. Spawn `executable` relative to that directory.

`schemaVersion` changes only for an incompatible manifest change.

## CI artifacts

[Build Electron helper](../../.github/workflows/build-helper.yml) also runs on PRs and pushes to `main` that touch `src/`, CMake, the helper packaging or this test, and on manual dispatch. It uses only the helper vcpkg manifest. Each artifact (`AutoSlidesExtractorHelper-macOS-arm64`, `-Windows-x64`, `-Linux-x86_64`) contains one versioned archive. A separate job unpacks each archive on a fresh native runner and runs the Electron CLI contract without a developer Qt install or a display. The job summary gives each archive's byte count and SHA-256.

## Building

The helper is built by [Build Electron helper](../../.github/workflows/build-helper.yml), and that workflow is the reference recipe. All three platforms follow the same steps; only the triplet and packager differ:

1. Install the extraction-only dependencies from this directory's manifest with the pinned vcpkg (`VCPKG_COMMIT` in the workflow) and the overlay triplets in `installer/vcpkg-triplets`: `vcpkg install --x-manifest-root=installer/helper --triplet <triplet> …`.
2. Configure with `-DAUTOSLIDES_ELECTRON_HELPER=ON` plus the vcpkg toolchain, and build the `AutoSlidesExtractorHelper` target. Only Qt Core is needed.
3. Package with `installer/package-helper-linux.sh`, `installer/package-helper-macos.sh` or `installer/package-helper-windows.ps1`. Each one checks that FFmpeg is the LGPL build from this manifest, collects only the real runtime dependencies and their notices, and writes the versioned archive.

| Platform | Runner | vcpkg triplet |
| --- | --- | --- |
| macOS arm64 | `macos-15` | `arm64-osx-dynamic-release` |
| Windows x64 | `windows-2022` (MSVC) | `x64-windows-release` |
| Linux x86-64 | `ubuntu-22.04` | `x64-linux-dynamic-release` |

FFmpeg decoder support is not restricted to H.264. The macOS archive is ad-hoc signed, not Developer ID signed or notarized, and the Windows EXE is not Authenticode-signed.

`tests/electron_contract.py --full <desktop executable> --helper <unpacked helper executable>` is the process-level parity test. It needs Python 3 and an FFmpeg CLI with libx264 to generate the fixture. CI runs it with `--helper` on every helper build and with both on a release tag.
