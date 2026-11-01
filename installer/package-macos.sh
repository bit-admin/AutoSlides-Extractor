#!/usr/bin/env zsh
# Package AutoSlides Extractor for macOS arm64 (.app + .dmg). Used by build-macos.yml.
#
# Packaging philosophy matches package-linux.sh and the Windows workflow:
#   - deps are discovered from the built binary (no hardcoded OpenCV/FFmpeg names)
#   - anything that cannot be bundled fails the build
#   - version comes from CMakeLists.txt
#
# Steps:
#   1. Stage build/AutoSlidesExtractor.app (path without spaces)
#   2. macdeployqt (Qt frameworks + plugins)
#   3. Bundle every other non-system dylib (vcpkg OpenCV/FFmpeg, ONNX Runtime)
#   4. Prune unused Qt plugins and unreachable Qt frameworks
#   5. Thin universal Qt binaries to arm64
#   6. Drop build-machine LC_RPATHs, strip local symbols
#   7. Info.plist (LSMinimumSystemVersion = highest minos found in the bundle)
#   8. Ad-hoc codesign (inside-out) + strict verify
#   9. Verify: no missing / external dylibs, VideoToolbox + ONNX present, --version runs
#  10. DMG (LZMA, "ULMO"): "AutoSlides Extractor.app" + /Applications link
#
# Usage:
#   installer/package-macos.sh [build-dir]        (default: build)
#
# Environment:
#   QT_PATH                  Qt prefix the app was built with (required; has bin/macdeployqt)
#   LIB_DIRS                 ':'-separated dirs to resolve non-Qt dylibs from (vcpkg lib dir)
#   MACOS_DEPLOYMENT_TARGET  macOS version our own code and vcpkg libs are built for
#                            (default 12.0). Any of those built for a newer macOS fails.
#   OUT_DIR                  where the .dmg is written (default: <build-dir>)
#
# Artifact:
#   AutoSlides.Extractor-<ver>-macOS-arm64.dmg
#
# The app is ad-hoc signed (no Developer ID): users clear quarantine once with
#   xattr -dr com.apple.quarantine "/Applications/AutoSlides Extractor.app"
# or System Settings → Privacy & Security → Open Anyway.

emulate -L zsh
set -euo pipefail
setopt NULL_GLOB EXTENDED_GLOB
autoload -Uz is-at-least

die()  { print -r -- "error: $*" >&2; exit 1; }
info() { print -r -- "==> $*"; }
log()  { print -r -- "    $*"; }

ROOT="${0:A:h:h}"
BUILD_DIR="${1:-$ROOT/build}"
[[ -d "$BUILD_DIR" ]] || die "build dir not found: $BUILD_DIR"
BUILD_DIR="${BUILD_DIR:A}"
OUT_DIR="${OUT_DIR:-$BUILD_DIR}"
DEPLOYMENT_TARGET="${MACOS_DEPLOYMENT_TARGET:-12.0}"
ARCH="arm64"

APP_NAME="AutoSlidesExtractor"
APP_DISPLAY_NAME="AutoSlides Extractor"
BUNDLE_ID="com.autoslides.extractor"

VERSION="$(sed -nE 's/^project\(AutoSlidesExtractor VERSION ([0-9.]+).*/\1/p' "$ROOT/CMakeLists.txt")"
[[ -n "$VERSION" ]] || die "could not parse project VERSION from CMakeLists.txt"
BASENAME="AutoSlides.Extractor-${VERSION}-macOS-${ARCH}"

SRC_APP="$BUILD_DIR/${APP_NAME}.app"
[[ -d "$SRC_APP" ]] || die "$SRC_APP not found (build first)"
[[ -f "$ROOT/resources/icon.icns" ]] || die "missing resources/icon.icns"

[[ -n "${QT_PATH:-}" ]] || die "QT_PATH is not set"
MACDEPLOYQT="$QT_PATH/bin/macdeployqt"
[[ -x "$MACDEPLOYQT" ]] || MACDEPLOYQT="$QT_PATH/libexec/macdeployqt"
[[ -x "$MACDEPLOYQT" ]] || die "macdeployqt not found under $QT_PATH"

LIB_SEARCH_DIRS=(${(s/:/)${LIB_DIRS:-}})

DIST_DIR="$BUILD_DIR/dist"
STAGE_APP="$DIST_DIR/${APP_NAME}.app"
CONTENTS="$STAGE_APP/Contents"
FRAMEWORKS_DIR="$CONTENTS/Frameworks"
PLUGINS_DIR="$CONTENTS/PlugIns"
EXECUTABLE="$CONTENTS/MacOS/$APP_NAME"

info "AutoSlides Extractor $VERSION — macOS $ARCH packaging"
log "app:          $SRC_APP"
log "macdeployqt:  $MACDEPLOYQT"
log "lib dirs:     ${LIB_SEARCH_DIRS[*]:-(none)}"
log "deploy target: macOS $DEPLOYMENT_TARGET"

# ---------------------------------------------------------------------------
# Mach-O helpers
# ---------------------------------------------------------------------------
is_macho() { file -b "$1" 2>/dev/null | grep -q 'Mach-O'; }

# Every Mach-O file in the staged bundle (executable, dylibs, plugins, framework binaries).
all_machos() {
  local f
  find "$STAGE_APP" -type f -print | while IFS= read -r f; do
    case "$f" in *.plist|*.prl|*.icns|*.qm|*.json|*.h) continue ;; esac
    if is_macho "$f"; then print -r -- "$f"; fi
  done
}

# Read only the arm64 slice: Qt ships universal binaries until step 5 thins
# them, and otool would otherwise print one section per architecture.
OTOOL=(otool -arch "$ARCH")

install_id() { "${OTOOL[@]}" -D "$1" 2>/dev/null | awk 'NR==2 {print; exit}'; }

# Dependency install names (otool -L), without the file's own id or header lines.
macho_deps() {
  local id; id="$(install_id "$1")"
  "${OTOOL[@]}" -L "$1" 2>/dev/null | awk -v id="$id" '/:$/ {next} $1 != id {print $1}'
}

macho_rpaths() {
  "${OTOOL[@]}" -l "$1" 2>/dev/null | awk '/cmd LC_RPATH/{getline; getline; if ($1=="path") print $2}'
}

# Minimum macOS a Mach-O was built for (LC_BUILD_VERSION minos or LC_VERSION_MIN_MACOSX).
macho_minos() {
  "${OTOOL[@]}" -l "$1" 2>/dev/null | awk '
    /^ *cmd / {cmd=$2}
    cmd=="LC_BUILD_VERSION" && $1=="minos" {print $2; exit}
    cmd=="LC_VERSION_MIN_MACOSX" && $1=="version" {print $2; exit}'
}

is_system_lib() {
  case "$1" in /System/*|/usr/lib/*) return 0 ;; esac
  return 1
}

is_qt_framework_ref() {
  case "$1" in *Qt*.framework/*) return 0 ;; esac
  return 1
}

# Resolve a dependency install name to a real file on this machine.
resolve_dep() {
  local dep="$1" referrer="$2" leaf rp
  case "$dep" in
    @rpath/*)
      leaf="${dep#@rpath/}"
      [[ -f "$FRAMEWORKS_DIR/$leaf" ]] && { print -r -- "$FRAMEWORKS_DIR/$leaf"; return 0; }
      for rp in ${(f)"$(macho_rpaths "$referrer")"} ${(f)"$(macho_rpaths "$EXECUTABLE")"}; do
        rp="${rp/#@loader_path/${referrer:h}}"
        rp="${rp/#@executable_path/${EXECUTABLE:h}}"
        [[ -f "$rp/$leaf" ]] && { print -r -- "$rp/$leaf"; return 0; }
      done
      for rp in "${LIB_SEARCH_DIRS[@]}"; do
        [[ -f "$rp/$leaf" ]] && { print -r -- "$rp/$leaf"; return 0; }
      done
      ;;
    @loader_path/*)
      [[ -f "${referrer:h}/${dep#@loader_path/}" ]] && { print -r -- "${referrer:h}/${dep#@loader_path/}"; return 0; }
      ;;
    @executable_path/*)
      [[ -f "${EXECUTABLE:h}/${dep#@executable_path/}" ]] && { print -r -- "${EXECUTABLE:h}/${dep#@executable_path/}"; return 0; }
      ;;
    /*)
      [[ -f "$dep" ]] && { print -r -- "$dep"; return 0; }
      ;;
  esac
  return 1
}

# ---------------------------------------------------------------------------
# 1. Stage
# ---------------------------------------------------------------------------
info "1. Stage app bundle"
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"
cp -R "$SRC_APP" "$STAGE_APP"
mkdir -p "$CONTENTS/Resources" "$FRAMEWORKS_DIR"
cp -f "$ROOT/resources/icon.icns" "$CONTENTS/Resources/icon.icns"
# Licence notice + GPL text (the bundled FFmpeg includes GPL x264)
mkdir -p "$CONTENTS/Resources/licenses"
cp -f "$ROOT/licenses/NOTICE.txt" "$ROOT/licenses/GPL-3.0.txt" "$CONTENTS/Resources/licenses/"
cp -f "$ROOT/LICENSE" "$CONTENTS/Resources/licenses/LICENSE-MIT.txt"

# ---------------------------------------------------------------------------
# 2. macdeployqt
# ---------------------------------------------------------------------------
info "2. macdeployqt"
"$MACDEPLOYQT" "$STAGE_APP" -always-overwrite -verbose=1

# ---------------------------------------------------------------------------
# 3. Bundle remaining non-system dylibs
# ---------------------------------------------------------------------------
# Copied dylibs get id @rpath/<name>; references that are not already
# @rpath/<name> or @executable_path/../Frameworks/<name> are rewritten to
# @rpath/<name>. The executable carries @executable_path/../Frameworks as its
# rpath, which dyld also uses for libraries further down the load chain.
info "3. Bundle non-system dylibs"
typeset -A seen
queue=("$EXECUTABLE")
for f in "$PLUGINS_DIR"/**/*(.N); do
  if is_macho "$f"; then queue+=("$f"); fi
done
for f in "$FRAMEWORKS_DIR"/*.dylib(.N); do queue+=("$f"); done

unresolved=0
while (( ${#queue[@]} )); do
  file="${queue[1]}"; queue=("${queue[@]:1}")
  [[ -n "${seen[$file]:-}" ]] && continue
  seen[$file]=1

  for dep in ${(f)"$(macho_deps "$file")"}; do
    [[ -z "$dep" ]] && continue
    is_system_lib "$dep" && continue
    is_qt_framework_ref "$dep" && continue      # macdeployqt's job; checked in step 9

    base="${dep:t}"
    [[ "$dep" == @rpath/* ]] && base="${dep#@rpath/}"
    dest="$FRAMEWORKS_DIR/$base"

    if [[ ! -f "$dest" ]]; then
      if ! src="$(resolve_dep "$dep" "$file")"; then
        print -r -- "    UNRESOLVED: $dep  (needed by ${file:t})" >&2
        unresolved=$((unresolved + 1))
        continue
      fi
      case "$src" in
        /opt/homebrew/*|/usr/local/*|/opt/local/*)
          die "${file:t} links $src — Homebrew/MacPorts library leaked into the build" ;;
      esac
      cp -fL "$src" "$dest"
      chmod u+w "$dest"
      install_name_tool -id "@rpath/$base" "$dest" || die "install_name_tool -id failed: $base"
      log "+ $base  (from $src)"
    fi

    case "$dep" in
      "@rpath/$base"|"@executable_path/../Frameworks/$base") ;;
      *) install_name_tool -change "$dep" "@rpath/$base" "$file" \
           || die "cannot rewrite $dep in ${file:t}" ;;
    esac
    if [[ -z "${seen[$dest]:-}" ]]; then queue+=("$dest"); fi
  done
done
(( unresolved == 0 )) || die "$unresolved unresolved dylib reference(s)"

if ! macho_rpaths "$EXECUTABLE" | grep -qx '@executable_path/../Frameworks'; then
  install_name_tool -add_rpath '@executable_path/../Frameworks' "$EXECUTABLE"
fi

# ---------------------------------------------------------------------------
# 4. Prune plugins and unreachable Qt frameworks
# ---------------------------------------------------------------------------
info "4. Prune unused Qt plugins and frameworks"
KEEP_PLUGIN_DIRS=(platforms imageformats iconengines styles)
for d in "$PLUGINS_DIR"/*(/N); do
  if (( ! ${KEEP_PLUGIN_DIRS[(Ie)${d:t}]} )); then
    log "- plugin dir ${d:t}"
    rm -rf "$d"
  fi
done
for p in "$PLUGINS_DIR"/imageformats/*.dylib(.N); do
  case "${p:t}" in
    libqjpeg.dylib|libqgif.dylib|libqico.dylib|libqicns.dylib|libqwebp.dylib|libqmacheif.dylib|libqmacjp2.dylib) ;;
    *) log "- imageformat ${p:t}"; rm -f "$p" ;;
  esac
done

framework_of() { print -r -- "$1" | sed -nE 's#.*/(Qt[^/]+\.framework)/.*#\1#p'; }
framework_bin() { local n="${1%.framework}"; print -r -- "$FRAMEWORKS_DIR/$1/Versions/A/$n"; }

typeset -A keep_fw
fw_queue=()
seed_from() {
  local dep fw
  for dep in ${(f)"$(macho_deps "$1")"}; do
    fw="$(framework_of "$dep")"
    [[ -n "$fw" && -d "$FRAMEWORKS_DIR/$fw" && -z "${keep_fw[$fw]:-}" ]] || continue
    keep_fw[$fw]=1
    fw_queue+=("$fw")
  done
}
seed_from "$EXECUTABLE"
for f in "$PLUGINS_DIR"/**/*(.N) "$FRAMEWORKS_DIR"/*.dylib(.N); do
  if is_macho "$f"; then seed_from "$f"; fi
done
while (( ${#fw_queue[@]} )); do
  fw="${fw_queue[1]}"; fw_queue=("${fw_queue[@]:1}")
  bin="$(framework_bin "$fw")"
  if [[ -f "$bin" ]]; then seed_from "$bin"; fi
done
for fw in "$FRAMEWORKS_DIR"/*.framework(/N); do
  if [[ -z "${keep_fw[${fw:t}]:-}" ]]; then
    log "- framework ${fw:t}"
    rm -rf "$fw"
  fi
done
log "kept frameworks: ${(ok)keep_fw}"

# ---------------------------------------------------------------------------
# 5. Thin to arm64
# ---------------------------------------------------------------------------
info "5. Thin universal binaries to $ARCH"
thinned=0
for f in ${(f)"$(all_machos)"}; do
  archs="$(lipo -archs "$f" 2>/dev/null || true)"
  [[ "$archs" == *" "* ]] || continue
  [[ " $archs " == *" $ARCH "* ]] || die "${f#$STAGE_APP/} has no $ARCH slice ($archs)"
  lipo -thin "$ARCH" "$f" -output "$f.thin" || die "lipo -thin failed: ${f#$STAGE_APP/}"
  mv "$f.thin" "$f"
  thinned=$((thinned + 1))
done
log "thinned $thinned file(s)"
exe_archs="$(lipo -archs "$EXECUTABLE")"
[[ "$exe_archs" == "$ARCH" ]] || die "executable is '$exe_archs', expected $ARCH"

# ---------------------------------------------------------------------------
# 6. Drop build-machine rpaths, strip
# ---------------------------------------------------------------------------
info "6. Remove absolute LC_RPATHs, strip local symbols"
for f in ${(f)"$(all_machos)"}; do
  for rp in ${(f)"$(macho_rpaths "$f")"}; do
    case "$rp" in
      @*) ;;
      *) install_name_tool -delete_rpath "$rp" "$f" || die "cannot delete rpath $rp from ${f:t}" ;;
    esac
  done
  strip -x "$f" 2>/dev/null || true
done

# ---------------------------------------------------------------------------
# 7. Info.plist
# ---------------------------------------------------------------------------
info "7. Minimum macOS + Info.plist"
min_macos="$DEPLOYMENT_TARGET"
min_from="(deployment target)"
too_new=0
for f in ${(f)"$(all_machos)"}; do
  v="$(macho_minos "$f")"
  [[ -n "$v" ]] || continue
  rel="${f#$CONTENTS/}"
  # Qt and ONNX Runtime are prebuilt and set their own floor; everything we
  # compile (app + vcpkg libs) must respect the deployment target.
  case "$rel" in
    Frameworks/Qt*.framework/*|PlugIns/*|Frameworks/libonnxruntime*) ;;
    *)
      if ! is-at-least "$v" "$DEPLOYMENT_TARGET"; then
        print -r -- "    built for macOS $v > $DEPLOYMENT_TARGET: $rel" >&2
        too_new=$((too_new + 1))
      fi
      ;;
  esac
  if ! is-at-least "$v" "$min_macos"; then
    min_macos="$v"; min_from="$rel"
  fi
done
(( too_new == 0 )) || die "$too_new binary(ies) built for a newer macOS than $DEPLOYMENT_TARGET (check the vcpkg triplet / CMAKE_OSX_DEPLOYMENT_TARGET)"
log "LSMinimumSystemVersion $min_macos  (set by $min_from)"

cat > "$CONTENTS/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>${APP_NAME}</string>
    <key>CFBundleIconFile</key>
    <string>icon.icns</string>
    <key>CFBundleIdentifier</key>
    <string>${BUNDLE_ID}</string>
    <key>CFBundleName</key>
    <string>${APP_DISPLAY_NAME}</string>
    <key>CFBundleDisplayName</key>
    <string>${APP_DISPLAY_NAME}</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>${VERSION}</string>
    <key>CFBundleVersion</key>
    <string>${VERSION}</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleGetInfoString</key>
    <string>${APP_DISPLAY_NAME} ${VERSION}</string>
    <key>NSHumanReadableCopyright</key>
    <string>Copyright © 2025–2026 AutoSlides Project. All rights reserved.</string>
    <key>LSMinimumSystemVersion</key>
    <string>${min_macos}</string>
    <key>LSRequiresNativeExecution</key>
    <true/>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSSupportsAutomaticGraphicsSwitching</key>
    <true/>
    <key>LSApplicationCategoryType</key>
    <string>public.app-category.productivity</string>
    <key>NSPrincipalClass</key>
    <string>NSApplication</string>
    <key>CFBundleDocumentTypes</key>
    <array>
        <dict>
            <key>CFBundleTypeName</key>
            <string>Video File</string>
            <key>CFBundleTypeRole</key>
            <string>Viewer</string>
            <key>LSItemContentTypes</key>
            <array>
                <string>public.movie</string>
                <string>public.video</string>
                <string>public.mpeg-4</string>
                <string>com.apple.quicktime-movie</string>
            </array>
            <key>LSHandlerRank</key>
            <string>Alternate</string>
        </dict>
    </array>
    <key>NSDesktopFolderUsageDescription</key>
    <string>AutoSlides Extractor needs access to save extracted slides.</string>
    <key>NSDocumentsFolderUsageDescription</key>
    <string>AutoSlides Extractor needs access to save extracted slides.</string>
    <key>NSDownloadsFolderUsageDescription</key>
    <string>AutoSlides Extractor needs access to save extracted slides.</string>
    <key>LSMultipleInstancesProhibited</key>
    <false/>
    <key>NSSupportsSuddenTermination</key>
    <true/>
    <key>NSSupportsAutomaticTermination</key>
    <true/>
</dict>
</plist>
EOF
plutil -lint "$CONTENTS/Info.plist" >/dev/null || die "Info.plist is invalid"

# ---------------------------------------------------------------------------
# 8. Ad-hoc codesign (inside-out)
# ---------------------------------------------------------------------------
info "8. Ad-hoc codesign"
sign() { codesign --force --sign - --timestamp=none "$1" || die "codesign failed: ${1#$STAGE_APP/}"; }
for f in "$FRAMEWORKS_DIR"/*.dylib(.N); do sign "$f"; done
for fw in "$FRAMEWORKS_DIR"/*.framework(/N); do sign "$fw"; done
for f in "$PLUGINS_DIR"/**/*(.N); do
  if is_macho "$f"; then sign "$f"; fi
done
sign "$EXECUTABLE"
sign "$STAGE_APP"
codesign --verify --deep --strict --verbose=1 "$STAGE_APP" || die "codesign verification failed"

# ---------------------------------------------------------------------------
# 9. Verify the bundle is self-contained
# ---------------------------------------------------------------------------
info "9. Verify bundle"
problems=0
problem() { print -r -- "    $*" >&2; problems=$((problems + 1)); }
for f in ${(f)"$(all_machos)"}; do
  rel="${f#$STAGE_APP/}"
  id="$(install_id "$f")"
  if [[ -n "$id" && "$id" == /* ]] && ! is_system_lib "$id"; then
    problem "absolute install id $id ($rel)"
  fi
  for rp in ${(f)"$(macho_rpaths "$f")"}; do
    [[ "$rp" == @* ]] || problem "absolute LC_RPATH $rp ($rel)"
  done
  for dep in ${(f)"$(macho_deps "$f")"}; do
    [[ -z "$dep" ]] && continue
    is_system_lib "$dep" && continue
    case "$dep" in
      @rpath/*)
        [[ -e "$FRAMEWORKS_DIR/${dep#@rpath/}" ]] || problem "missing $dep (needed by $rel)" ;;
      @executable_path/../Frameworks/*)
        [[ -e "$FRAMEWORKS_DIR/${dep#@executable_path/../Frameworks/}" ]] || problem "missing $dep (needed by $rel)" ;;
      @loader_path/*)
        [[ -e "${f:h}/${dep#@loader_path/}" ]] || problem "missing $dep (needed by $rel)" ;;
      *)
        problem "external dependency $dep (needed by $rel)" ;;
    esac
  done
done
(( problems == 0 )) || die "$problems dependency problem(s) in the bundle"

avcodec=("$FRAMEWORKS_DIR"/libavcodec*.dylib(.N))
(( ${#avcodec[@]} )) || die "libavcodec not bundled"
otool -L "${avcodec[1]}" | grep -q 'VideoToolbox.framework' \
  || die "bundled FFmpeg was built without VideoToolbox (no hardware decoding)"
otool -L "${avcodec[1]}" | grep -q 'libx264' \
  || die "bundled FFmpeg was built without libx264 (no video export)"
onnx=("$FRAMEWORKS_DIR"/libonnxruntime*.dylib(.N))
(( ${#onnx[@]} )) || die "ONNX Runtime not bundled (ML classification would be unavailable)"
log "OK: no missing or external dylibs; VideoToolbox + ONNX Runtime present"

launch_out="$("$EXECUTABLE" --version 2>&1)" || die "--version failed: $launch_out"
log "--version: $launch_out"

# ---------------------------------------------------------------------------
# 10. DMG
# ---------------------------------------------------------------------------
info "10. Create DMG"
DMG_ROOT="$DIST_DIR/dmg"
DMG_PATH="$OUT_DIR/$BASENAME.dmg"
rm -rf "$DMG_ROOT" "$DMG_PATH"
mkdir -p "$DMG_ROOT" "$OUT_DIR"
ditto "$STAGE_APP" "$DMG_ROOT/${APP_DISPLAY_NAME}.app"
ln -s /Applications "$DMG_ROOT/Applications"
codesign --verify --deep --strict "$DMG_ROOT/${APP_DISPLAY_NAME}.app" || die "signature broken after copy"

# ULMO = LZMA (smallest; macOS 10.15+). hdiutil is occasionally "Resource busy" on CI.
for attempt in 1 2 3; do
  if hdiutil create -volname "$APP_DISPLAY_NAME $VERSION" -srcfolder "$DMG_ROOT" \
       -fs HFS+ -format ULMO -ov "$DMG_PATH"; then
    break
  fi
  (( attempt < 3 )) || die "hdiutil create failed"
  log "hdiutil create failed, retrying in 5 s"
  sleep 5
done
hdiutil verify "$DMG_PATH" >/dev/null || die "hdiutil verify failed"

info "Done"
log "DMG:  $DMG_PATH ($(du -h "$DMG_PATH" | awk '{print $1}'))"
log "App:  $(du -sh "$STAGE_APP" | awk '{print $1}'), $(ls "$FRAMEWORKS_DIR" | wc -l | tr -d ' ') entries in Frameworks"
log "Min:  macOS $min_macos"
