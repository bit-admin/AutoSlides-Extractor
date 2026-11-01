# vcpkg overlay triplet for the Linux release build (build-linux.yml).
#
# The community x64-linux-dynamic triplet (FFmpeg/OpenCV as .so next to the
# app), release only: the app is only built in Release, so skip the debug
# builds of FFmpeg/OpenCV and roughly halve a cold vcpkg build.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_BUILD_TYPE release)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_FIXUP_ELF_RPATH ON)
