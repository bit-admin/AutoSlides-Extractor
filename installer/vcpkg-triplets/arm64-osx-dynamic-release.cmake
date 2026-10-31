# vcpkg overlay triplet for the macOS release build (build-macos.yml).
#
# Same idea as x64-linux-dynamic / x64-windows: OpenCV and FFmpeg ship as
# dylibs inside the app bundle (FFmpeg stays dynamically linked for LGPL).
#   - arm64 only (Apple Silicon)
#   - release only: the app is only built in Release, so skip debug builds
#   - deployment target pinned, otherwise vcpkg builds for the runner's own
#     macOS version and the bundle will not launch on older systems.
#     Keep in sync with MACOS_DEPLOYMENT_TARGET in build-macos.yml.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_BUILD_TYPE release)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)
set(VCPKG_OSX_DEPLOYMENT_TARGET 12.0)
