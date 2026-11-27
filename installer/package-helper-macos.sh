#!/bin/zsh
# Package the Release console helper (not the desktop .app). No macdeployqt,
# GUI plugins, or runtime environment variables are needed.
# QT_PATH=/path/to/qt VCPKG_PREFIX=/helper/vcpkg_installed/<triplet> \
#   [OUT_DIR=/destination] installer/package-helper-macos.sh <build-dir>
# Output: AutoSlidesExtractorHelper-<version>-macOS-<arch>.tar.gz, containing
# AutoSlidesExtractorHelper/{AutoSlidesExtractorHelper,lib/,licenses/}.
emulate -L zsh
set -euo pipefail

die() { print -ru2 -- "error: $*"; exit 1; }
[[ $# == 1 ]] || die "usage: QT_PATH=... VCPKG_PREFIX=... $0 <build-dir>"
[[ -n ${QT_PATH:-} && -n ${VCPKG_PREFIX:-} ]] || die 'set QT_PATH and VCPKG_PREFIX (helper-only vcpkg triplet)'
root=${0:A:h:h}
name=AutoSlidesExtractorHelper
build=${1:A}
exe=$build/$name
out=${OUT_DIR:-$build}
qtlib=${QT_PATH:A}/lib
prefix=${VCPKG_PREFIX:A}
triplet=${prefix:t}
qtbin=$qtlib/QtCore.framework/Versions/A/QtCore
[[ -x $exe && -f $qtbin && -d $prefix/lib ]] || die 'missing helper, QtCore framework, or vcpkg lib directory'
# The pinned FFmpeg port chooses its copyright file from configure's actual
# licence result. Only the helper manifest's LGPL build may be redistributed.
grep -Eq '^[[:space:]]*GNU LESSER GENERAL PUBLIC LICENSE[[:space:]]*$' "$prefix/share/ffmpeg/copyright" || die 'not an LGPL FFmpeg build (use the installer/helper vcpkg manifest)'
version=$(sed -nE 's/^project\(AutoSlidesExtractor VERSION ([0-9.]+).*/\1/p' "$root/CMakeLists.txt")
[[ -n $version ]] || die 'cannot determine project version'
arch=$(lipo -archs "$exe")
[[ $arch == arm64 || $arch == x86_64 ]] || die "expected a single arm64 or x86_64 target architecture, got '$arch'"

mkdir -p "$out"
work=$(mktemp -d "${out:A}/.helper-package.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
stage=$work/$name
lib=$stage/lib
lic=$stage/licenses
mkdir -p "$lib" "$lic"
cp "$root/LICENSE" "$lic/LICENSE-MIT.txt"
cp "$root/licenses/NOTICE-helper.txt" "$lic/NOTICE.txt"
cp "$root/licenses/GPL-3.0.txt" "$root/licenses/LGPL-3.0.txt" "$lic/"
print -r -- 'This helper uses LGPL FFmpeg; see ffmpeg.txt for the licence selected by its build configuration. No desktop GPL codec bundle is included.' >> "$lic/NOTICE.txt"

# Read the selected slice's load commands, preserving embedded spaces. In
# particular, an LC_ID_DYLIB is not an edge back to the same library.
loads() {
  otool -arch "$arch" -l "$1" | awk -v kind="$2" '
    $1 == "cmd" {cmd=$2}
    (kind == "dep" && cmd ~ /^LC_(LOAD|LOAD_WEAK|REEXPORT|LOAD_UPWARD|LAZY_LOAD)_DYLIB$/ && $1 == "name") ||
    (kind == "id" && cmd == "LC_ID_DYLIB" && $1 == "name") ||
    (kind == "rpath" && cmd == "LC_RPATH" && $1 == "path") {
      sub(/^[ \t]*(name|path) /, ""); sub(/ \(offset [0-9]+\)$/, ""); print
    }'
}
system_dep() { [[ $1 == /System/Library/* || $1 == /usr/lib/* ]]; }

# Resolve only from the two declared release inputs. Never fall back to a
# Homebrew, desktop build, or other library of the same name on the host.
resolve() {
  local dep=$1 src=$2 found
  case $dep in
    @rpath/QtCore.framework/*) found=$qtlib/${dep#@rpath/} ;;
    @rpath/*) found=$prefix/lib/${dep#@rpath/} ;;
    @loader_path/*) found=${src:h}/${dep#@loader_path/} ;;
    @executable_path/*) found=$build/${dep#@executable_path/} ;;
    /*) found=$dep ;;
    *) die "unsupported dependency $dep in $src" ;;
  esac
  [[ -f $found ]] || die "cannot resolve $dep in $src (looked for $found)"
  found=${found:A}
  [[ $found == $qtbin || ( ${found:h} == $prefix/lib && $found == *.dylib ) ]] || die "unexpected non-system dependency: $found (from $src)"
  case ${found:t} in
    libx264*|libx265*|libopus*|libonnxruntime*|libQt*) die "unexpected desktop dependency: $found" ;;
  esac
  print -r -- "$found"
}

# Copy notices for the actual owners of the dylibs, not every port installed
# (which would include build tools). vcpkg file lists also handle versioned IDs.
port_lists=("${prefix:h}"/vcpkg/info/*_"$triplet".list(N))
(( $#port_lists )) || die "no vcpkg package inventory found for $prefix"
copy_port_notice() {
  local src=$1 inventory owner
  inventory=$(grep -Fxl -- "$triplet/lib/${src:t}" "${port_lists[@]}") || die "no vcpkg owner for $src"
  [[ $inventory != *$'\n'* ]] || die "multiple vcpkg owners for $src"
  owner=${inventory:t}; owner=${owner%%_*}
  [[ -s $prefix/share/$owner/copyright ]] || die "missing $owner copyright"
  cp "$prefix/share/$owner/copyright" "$lic/$owner.txt"
}

# Each real library gets a single canonical install name. All edges use
# @rpath; only the executable has a runpath, rooted in its own directory.
qt_rel=QtCore.framework/Versions/A/QtCore
typeset -A dest
queue=("$exe")
dest[$exe]=$stage/$name
for (( i=1; i <= $#queue; ++i )); do
  src=${queue[i]}
  dst=${dest[$src]}
  if [[ $src == $qtbin ]]; then
    fw=$lib/QtCore.framework
    resources=${dst:h}/Resources
    mkdir -p "$resources"
    cp "${src:h}/Resources/Info.plist" "$resources/"
    privacy=${src:h}/Resources/PrivacyInfo.xcprivacy
    if [[ -f $privacy ]]; then cp "$privacy" "$resources/"; fi
    ln -s A "$fw/Versions/Current"
    ln -s Versions/Current/QtCore "$fw/QtCore"
    ln -s Versions/Current/Resources "$fw/Resources"
    # Official kits that include an SBOM identify the third-party code in Qt.
    sboms=("${QT_PATH:A}"/sbom/qtbase-<->.<->.<->.spdx(N))
    if (( $#sboms )); then cp "${sboms[@]}" "$lic/"; fi
  elif [[ $src != $exe ]]; then
    copy_port_notice "$src"
  fi

  slices=$(lipo -archs "$src")
  case " $slices " in *" $arch "*) ;; *) die "missing $arch in $src ($slices)" ;; esac
  if [[ $slices == "$arch" ]]; then cp "$src" "$dst"; else lipo "$src" -thin "$arch" -output "$dst"; fi
  chmod u+w "$dst"
  fixups=()
  if [[ $src != $exe ]]; then fixups+=(-id "@rpath/${dst#$lib/}"); fi
  deps=$(loads "$dst" dep)
  for dep in "${(@f)deps}"; do
    [[ -n $dep ]] || continue
    system_dep "$dep" && continue
    target=$(resolve "$dep" "$src")
    rel=${target:t}
    if [[ $target == $qtbin ]]; then rel=$qt_rel; fi
    if [[ -z ${dest[$target]:-} ]]; then
      dest[$target]=$lib/$rel
      queue+=("$target")
    fi
    if [[ $dep != "@rpath/$rel" ]]; then fixups+=(-change "$dep" "@rpath/$rel"); fi
  done
  rpaths=$(loads "$dst" rpath)
  for rp in "${(@f)rpaths}"; do
    [[ -n $rp ]] && fixups+=(-delete_rpath "$rp")
  done
  if [[ $src == $exe ]]; then fixups+=(-add_rpath '@executable_path/lib'); fi
  if (( $#fixups )); then install_name_tool "${fixups[@]}" "$dst"; fi
  strip -x "$dst"
done

[[ -f $lib/$qt_rel && -s $lic/ffmpeg.txt && -s $lic/opencv4.txt ]] || die 'expected QtCore, FFmpeg and OpenCV in helper closure'

# Verify the final paths and architecture, not just the planned edits. Sign
# the versioned framework as a bundle; verify each independent code object.
for src in "${queue[@]}"; do
  dst=${dest[$src]}
  [[ $(lipo -archs "$dst") == "$arch" ]] || die "bad architecture: $dst"
  rpaths=$(loads "$dst" rpath)
  if [[ $src == $exe ]]; then
    [[ $rpaths == '@executable_path/lib' ]] || die "bad helper LC_RPATH: $rpaths"
  else
    [[ -z $rpaths && $(loads "$dst" id) == "@rpath/${dst#$lib/}" ]] || die "unexpected LC_RPATH or install ID: $dst"
  fi
  deps=$(loads "$dst" dep)
  for dep in "${(@f)deps}"; do
    [[ -n $dep ]] || continue
    system_dep "$dep" && continue
    [[ $dep == @rpath/* && $dep != */../* && -f $lib/${dep#@rpath/} ]] || die "unresolved or external dependency $dep in $dst"
  done
done
for obj in "$lib"/*.dylib "$lib/QtCore.framework" "$stage/$name"; do
  codesign --force --sign - --timestamp=none "$obj"
  codesign --verify --strict "$obj"
done

archive=$name-$version-macOS-$arch.tar.gz
COPYFILE_DISABLE=1 /usr/bin/tar --no-xattrs -czf "$work/$archive" -C "$work" "$name"
mv -f "$work/$archive" "$out/$archive"
print -r -- "Packaged $out/$archive"
