#!/usr/bin/env bash
# On Ubuntu 22.04 x86_64 with binutils and patchelf:
# QT_PATH=$QT_ROOT_DIR VCPKG_PREFIX=/helper/vcpkg_installed/<triplet> \
#   [OUT_DIR=/destination] installer/package-helper-linux.sh <build-dir>
# Spawn AutoSlidesExtractorHelper/AutoSlidesExtractorHelper directly from the
# unpacked archive. No launcher, LD_LIBRARY_PATH, Qt plugins or X server needed.
set -euo pipefail
export LC_ALL=C

die() { printf 'error: %s\n' "$*" >&2; exit 1; }
[[ $# == 1 && -n ${QT_PATH:-} && -n ${VCPKG_PREFIX:-} ]] || die "usage: QT_PATH=... VCPKG_PREFIX=... $0 <build-dir>"
[[ $(uname -sm) == 'Linux x86_64' ]] || die 'requires native x86_64 Linux'
root=$(realpath "$(dirname "$0")/..")
build=$(realpath "$1")
prefix=$(realpath "$VCPKG_PREFIX")
qt=$(realpath "$QT_PATH")
triplet=${prefix##*/}
name=AutoSlidesExtractorHelper
[[ -x $build/$name && -d $prefix/lib && -f $qt/lib/libQt6Core.so.6 ]] || die 'missing helper, helper-only vcpkg prefix, or Qt Core'
version=$(sed -nE 's/^project\(AutoSlidesExtractor VERSION ([0-9.]+).*/\1/p' "$root/CMakeLists.txt")
[[ -n $version ]] || die 'cannot determine project version'
# The pinned FFmpeg port chooses this file based on configure's actual licence.
grep -Eq '^[[:space:]]*GNU LESSER GENERAL PUBLIC LICENSE[[:space:]]*$' "$prefix/share/ffmpeg/copyright" || die 'not an LGPL FFmpeg build'

out=${OUT_DIR:-$build}
mkdir -p "$out"
out=$(realpath "$out")
work=$(mktemp -d "$out/.helper-package.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
stage=$work/$name
lib=$stage/lib
lic=$stage/licenses
mkdir -p "$lib" "$lic"
install -m644 "$root/LICENSE" "$lic/LICENSE-MIT.txt"
install -m644 "$root/licenses/NOTICE-helper-linux.txt" "$lic/NOTICE.txt"
cp "$root/licenses/LGPL-3.0.txt" "$root/licenses/GPL-3.0.txt" "$lic/"
shopt -s nullglob
for sbom in "$qt"/sbom/qtbase-[0-9]*.spdx; do
    [[ $sbom == *.source.spdx ]] || cp "$sbom" "$lic/"
done
lists=("${prefix%/*}"/vcpkg/info/*_"$triplet".list)
(( ${#lists[@]} )) || die "missing vcpkg inventory for $triplet"

# Only the base OS ABI is left to the host. Other headless system DSOs needed
# by the official Qt kit (e.g. GLib -> libpcre.so.3 on Jammy) travel with it.
host_abi() {
    case $1 in
        ld-linux-x86-64.so.2|libc.so.6|libm.so.6|libpthread.so.0|libdl.so.2|librt.so.1|libresolv.so.2|libgcc_s.so.1|libstdc++.so.6) return 0 ;;
        *) return 1 ;;
    esac
}

copy_port_notice() {
    local inventory port file=${1##*/}
    inventory=$(grep -Fxl -- "$triplet/lib/$file" "${lists[@]}") || die "no vcpkg owner for $file"
    [[ $inventory != *$'\n'* ]] || die "multiple vcpkg owners for $file"
    port=${inventory##*/}; port=${port%%_*}
    [[ -s $prefix/share/$port/copyright ]] || die "missing $port copyright"
    cp "$prefix/share/$port/copyright" "$lic/$port.txt"
}

copy_ubuntu_notice() {
    local src=$1 rel owner pkg notice common
    # dpkg's file list may use either /lib or /usr/lib on merged-/usr.
    rel=${src#/usr}
    owner=$(dpkg-query -S "$rel" 2>/dev/null || dpkg-query -S "/usr$rel") || die "no package owns $src"
    pkg=${owner%%:*}
    notice=$lic/ubuntu-$pkg.txt
    [[ ! -f $notice ]] || return 0
    cp -L "/usr/share/doc/$pkg/copyright" "$notice"
    for common in /usr/share/common-licenses/*; do
        if grep -Fq "$common" "$notice"; then cp -L "$common" "$lic/ubuntu-${common##*/}.txt"; fi
    done
}

install -m755 "$build/$name" "$stage/$name"
loader=/lib64/ld-linux-x86-64.so.2
[[ $(patchelf --print-interpreter "$stage/$name") == "$loader" ]] || die 'expected a glibc x86_64 executable'
queue=("$stage/$name")
for ((i=0; i<${#queue[@]}; i++)); do
    obj=${queue[i]}
    # A DT_RUNPATH is not inherited through indirect dependencies. Patch every
    # DSO, with its own $ORIGIN, not just the helper or its first-level deps.
    rpath='$ORIGIN'
    if [[ $obj == "$stage/$name" ]]; then rpath='$ORIGIN/lib'; fi
    strip --strip-unneeded "$obj"
    patchelf --set-rpath "$rpath" "$obj"
    [[ $(patchelf --print-rpath "$obj") == "$rpath" ]] || die "unexpected runpath in $obj"
    needed=$(patchelf --print-needed "$obj")
    while IFS= read -r dep; do
        [[ -n $dep ]] || continue
        [[ $dep != */* ]] || die "nonportable DT_NEEDED $dep in $obj"
        host_abi "$dep" && continue
        [[ ! -f $lib/$dep ]] || continue
        # Resolve libraries by DT_NEEDED (not a glob of all files in a kit).
        # In particular, never satisfy a missing FFmpeg/OpenCV DSO from /usr.
        case $dep in
            libQt6Core.so.6)
                src=$qt/lib/$dep ;;
            libicuuc.so.73|libicui18n.so.73|libicudata.so.73)
                src=$qt/lib/$dep
                [[ $(realpath -e "$src") == "$qt/lib/"*.so.73.2 ]] || die "expected Qt kit's ICU 73.2: $src"
                cp "$root/licenses/ICU-73.2-linux-helper.txt" "$lic/ICU-73.2.txt" ;;
            libavcodec.so.*|libavformat.so.*|libavutil.so.*|libswscale.so.*|libswresample.so.*|libopencv_core.so.*|libopencv_core4.so.*|libopencv_imgproc.so.*|libopencv_imgproc4.so.*|libopencv_imgcodecs.so.*|libopencv_imgcodecs4.so.*|libpng*.so.*|libz.so.1)
                src=$(realpath -e "$prefix/lib/$dep") || die "missing vcpkg library $dep"
                [[ ${src%/*} == "$prefix/lib" ]] || die "vcpkg library escapes prefix: $src"
                copy_port_notice "$src" ;;
            libglib-2.0.so.*|libgthread-2.0.so.*|libpcre.so.*|libpcre2-*.so.*|libzstd.so.*|libdouble-conversion.so.*|libatomic.so.*|libgomp.so.*)
                src=$(realpath -e "/lib/x86_64-linux-gnu/$dep") || die "missing Ubuntu runtime library $dep"
                copy_ubuntu_notice "$src" ;;
            *) die "unexpected dependency $dep in $obj (not in headless helper profile)" ;;
        esac
        [[ $(patchelf --print-soname "$src") == "$dep" ]] || die "unexpected SONAME for $src"
        install -m755 "$src" "$lib/$dep"
        queue+=("$lib/$dep")
    done <<< "$needed"
done
[[ -f $lib/libQt6Core.so.6 && -s $lic/ffmpeg.txt && -s $lic/opencv4.txt ]] || die 'expected Qt Core, FFmpeg and OpenCV in closure'

# Reject accidental builds on a newer base rather than silently promising
# Ubuntu 22.04 for an ELF requiring newer glibc/libstdc++ symbols.
versions=$(readelf --wide --version-info "${queue[@]}")
check_abi_floor() {
    local family=$1 limit=$2 newest
    newest=$(sed -nE "s/.*Name: ${family}_([0-9.]+) .*/\\1/p" <<< "$versions" | sort -V | tail -n 1)
    [[ -z $newest ]] || printf '%s\n%s\n' "$newest" "$limit" | sort -VC || die "helper requires ${family}_$newest (Jammy limit: $limit)"
}
check_abi_floor GLIBC 2.35
check_abi_floor GLIBCXX 3.4.30
check_abi_floor CXXABI 1.3.13

# Ask the native loader for its actual clean resolution; never execute via
# qmake/linuxdeploy wrappers or with development/host search paths injected.
resolved=$(env -i "$loader" --inhibit-cache --list "$stage/$name")
for obj in "$lib"/*; do
    [[ $resolved == *"${obj##*/} => $obj ("* ]] || die "loader did not resolve bundled $obj"
done
env -i PATH=/usr/bin:/bin HOME="$work" "$stage/$name" --version --json

archive=$name-$version-Linux-x86_64.tar.gz
tar --owner=0 --group=0 --numeric-owner -czf "$work/$archive" -C "$work" "$name"
mv -f "$work/$archive" "$out/$archive"
printf 'Packaged %s\n' "$out/$archive"
