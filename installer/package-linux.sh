#!/usr/bin/env bash
# Package AutoSlides Extractor for Linux x86_64 (AppImage + portable tar.gz).
#
# Packaging philosophy matches the Windows workflow / package-macos.sh:
#   - discover deps from the built binary (no hardcoded OpenCV/FFmpeg sonames)
#   - fail hard on missing critical libraries
#   - verify with --version only
#   - version comes from CMakeLists.txt
#
# Usage:
#   installer/package-linux.sh [build-dir]        (default: build)
#
# Environment:
#   QMAKE       qmake of the Qt the app was built against (default: qmake6/qmake in PATH)
#   OUT_DIR     where the artifacts are written (default: <build-dir>)
#   TOOLS_DIR   cache for the linuxdeploy AppImages (default: <build-dir>/tools)
#   LIB_DIRS    extra ':'-separated dirs holding shared libs to bundle (e.g. vcpkg lib)
#
# Library search paths are otherwise taken from the RUNPATH CMake gives the build-tree
# binary (ONNX Runtime vendor dir, OpenCV). FFmpeg is linked by name via pkg-config, so
# pass its lib dir in LIB_DIRS when it is not in a system location.
#
# Artifacts:
#   AutoSlides.Extractor-<ver>-Linux-x86_64.AppImage
#   AutoSlides.Extractor-<ver>-Linux-x86_64-Portable.tar.gz

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$(cd "${1:-$ROOT/build}" && pwd)"
OUT_DIR="${OUT_DIR:-$BUILD_DIR}"
TOOLS_DIR="${TOOLS_DIR:-$BUILD_DIR/tools}"
APP=AutoSlidesExtractor

# Pinned so a moving "continuous" tag cannot change the package layout under us.
LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_QT_URL="https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage"
LINUXDEPLOY_APPIMAGE_URL="https://github.com/linuxdeploy/linuxdeploy-plugin-appimage/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-appimage-x86_64.AppImage"

die() { printf 'error: %b\n' "$*" >&2; exit 1; }

VERSION="$(sed -nE 's/^project\(AutoSlidesExtractor VERSION ([0-9.]+).*/\1/p' "$ROOT/CMakeLists.txt")"
[ -n "$VERSION" ] || die "could not parse project VERSION from CMakeLists.txt"
BASENAME="AutoSlides.Extractor-$VERSION-Linux-x86_64"

BIN="$BUILD_DIR/$APP"
[ -x "$BIN" ] || die "$BIN not found (build first)"

if [ -z "${QMAKE:-}" ]; then
    QMAKE="$(command -v qmake6 || command -v qmake || true)"
fi
[ -n "$QMAKE" ] && [ -x "$QMAKE" ] || die "qmake not found; set QMAKE to the Qt used for the build"
export QMAKE

echo "Version:   $VERSION"
echo "Binary:    $BIN"
echo "qmake:     $QMAKE ($("$QMAKE" -query QT_VERSION))"

# --- linuxdeploy tools ---------------------------------------------------------
mkdir -p "$TOOLS_DIR"
for url in "$LINUXDEPLOY_URL" "$LINUXDEPLOY_QT_URL" "$LINUXDEPLOY_APPIMAGE_URL"; do
    f="$TOOLS_DIR/$(basename "$url")"
    if [ ! -x "$f" ]; then
        echo "Downloading $url"
        curl -fsSL --retry 3 -o "$f.tmp" "$url"
        mv "$f.tmp" "$f"
        chmod +x "$f"
    fi
done
# The tools are AppImages themselves; extract-and-run avoids needing FUSE (CI, containers).
export APPIMAGE_EXTRACT_AND_RUN=1
export PATH="$TOOLS_DIR:$PATH"

# --- library search path from the build-tree RUNPATH ----------------------------
# `cmake --install` strips the build RPATH, so linuxdeploy must be told where the
# vendor ONNX Runtime / vcpkg shared libraries live.
RUNPATH="$(patchelf --print-rpath "$BIN" 2>/dev/null || true)"
LIB_PATH=""
IFS=: read -r -a rp_dirs <<< "${LIB_DIRS:+$LIB_DIRS:}$RUNPATH"
for d in "${rp_dirs[@]}"; do
    [ -n "$d" ] && [ -d "$d" ] && LIB_PATH="${LIB_PATH:+$LIB_PATH:}$d"
done
echo "Lib dirs:  ${LIB_PATH:-<none>}"
export LD_LIBRARY_PATH="${LIB_PATH}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

if ldd "$BIN" | grep -q "not found"; then
    ldd "$BIN" | grep "not found" >&2
    die "build-tree binary has unresolved libraries"
fi

# --- AppDir ----------------------------------------------------------------------
APPDIR="$BUILD_DIR/AppDir"
rm -rf "$APPDIR"
cmake --install "$BUILD_DIR" --prefix "$APPDIR/usr" > /dev/null

DESKTOP="$APPDIR/usr/share/applications/$APP.desktop"
ICON="$APPDIR/usr/share/icons/hicolor/256x256/apps/$APP.png"
[ -f "$DESKTOP" ] || die "install did not produce $DESKTOP"
[ -f "$ICON" ] || die "install did not produce $ICON"

cd "$OUT_DIR"
rm -f "$BASENAME.AppImage"
# LDAI_OUTPUT names the AppImage; plugin-qt deploys the Qt plugins (xcb, imageformats,
# ...) and writes qt.conf. Libraries on the AppImage excludelist (glibc, libGL, X11,
# fontconfig, ...) are intentionally left to the host system.
LDAI_OUTPUT="$BASENAME.AppImage" \
    linuxdeploy-x86_64.AppImage \
        --appdir "$APPDIR" \
        --executable "$APPDIR/usr/bin/$APP" \
        --desktop-file "$DESKTOP" \
        --icon-file "$ICON" \
        --plugin qt \
        --output appimage
[ -f "$BASENAME.AppImage" ] || die "AppImage not produced"
chmod +x "$BASENAME.AppImage"

# --- verify bundled dependencies ------------------------------------------------
# Resolve against the AppDir only (no LD_LIBRARY_PATH) so a missing bundle is caught.
missing="$(env -u LD_LIBRARY_PATH ldd "$APPDIR/usr/bin/$APP" | grep "not found" || true)"
[ -z "$missing" ] || die "unresolved libraries in AppDir:\n$missing"

for pattern in \
    'libQt6Core.so*' 'libQt6Gui.so*' 'libQt6Widgets.so*' \
    'libopencv_core*.so*' 'libopencv_imgproc*.so*' 'libopencv_imgcodecs*.so*' \
    'libavcodec.so*' 'libavformat.so*' 'libavutil.so*' 'libswscale.so*' \
    'libonnxruntime.so*'; do
    # Static OpenCV/FFmpeg builds are linked into the binary instead; only fail if the
    # binary actually needs a shared copy that was not bundled.
    hit="$(find "$APPDIR/usr/lib" -maxdepth 1 -name "$pattern" -print -quit)"
    if [ -n "$hit" ]; then
        echo "  OK $pattern -> $(basename "$hit")"
    elif readelf -d "$APPDIR/usr/bin/$APP" | grep NEEDED | grep -q "\[${pattern%%\**}"; then
        die "required library missing from AppDir: $pattern"
    else
        echo "  -- $pattern (not a shared dependency)"
    fi
done
[ -f "$APPDIR/usr/plugins/platforms/libqxcb.so" ] || die "Qt xcb platform plugin missing"

# Oldest glibc the bundle runs on = newest GLIBC_x.y symbol version it references.
GLIBC_MIN="$(find "$APPDIR/usr" -type f \( -name '*.so*' -o -path '*/bin/*' \) -print0 |
    xargs -0 objdump -T 2>/dev/null | grep -o 'GLIBC_[0-9][0-9.]*' | sort -Vu | tail -1 || true)"
GLIBC_MIN="${GLIBC_MIN#GLIBC_}"
echo "Requires glibc >= ${GLIBC_MIN:-unknown}"

# --- portable tarball -----------------------------------------------------------
# Same tree as the AppImage, for systems without FUSE or for headless CLI use:
#   ./AutoSlidesExtractor            (GUI, or CLI when given arguments)
STAGE="$BUILD_DIR/portable/$BASENAME"
rm -rf "$BUILD_DIR/portable"
mkdir -p "$STAGE"
cp -a "$APPDIR/." "$STAGE/"
ln -s AppRun "$STAGE/$APP"
cat > "$STAGE/README.txt" <<EOF
AutoSlides Extractor - Linux Release
Version: $VERSION

Usage:
1. Extract this archive anywhere (keep the directory layout intact).
2. Run ./$APP
3. CLI: ./$APP --help  (or Settings > CLI to install the SlidesExtractor command)

Notes:
- Portable CI build for x86_64 (requires a CPU with AVX2 and glibc ${GLIBC_MIN:-?}+).
- ML uses ONNX Runtime on the CPU. CUDA is not redistributed.
- The AppImage release contains the same files in a single executable.
EOF
tar -C "$BUILD_DIR/portable" -czf "$OUT_DIR/$BASENAME-Portable.tar.gz" "$BASENAME"

echo
echo "AppImage: $OUT_DIR/$BASENAME.AppImage ($(du -h "$OUT_DIR/$BASENAME.AppImage" | cut -f1))"
echo "Portable: $OUT_DIR/$BASENAME-Portable.tar.gz ($(du -h "$OUT_DIR/$BASENAME-Portable.tar.gz" | cut -f1))"
