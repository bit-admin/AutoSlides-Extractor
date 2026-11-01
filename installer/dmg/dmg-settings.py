# dmgbuild settings for the macOS DMG window (used by installer/package-macos.sh).
#
#   dmgbuild -s installer/dmg/dmg-settings.py \
#            -D app="<path>/AutoSlides Extractor.app" \
#            -D background=installer/dmg/background.png \
#            "<volume name>" out.dmg
#
# dmgbuild writes the Finder layout (.DS_Store) itself, so no Finder/AppleScript
# is needed on CI. background@2x.png next to background.png is picked up and
# combined into a Retina TIFF. Icon positions must match background.html.
#
# Needs dmgbuild >= 1.6.7: up to 1.6.6 it also wrote a background bookmark
# (pBBk), and Finder on macOS 26.2+ then shows no background at all.
import os.path

app = defines["app"]  # noqa: F821 (provided by dmgbuild)
app_name = os.path.basename(app)

format = "ULMO"  # LZMA, smallest; macOS 10.15+
filesystem = "HFS+"

files = [app]  # copied with ditto, so the ad-hoc signature survives
symlinks = {"Applications": "/Applications"}

background = defines["background"]  # noqa: F821
window_rect = ((200, 120), (640, 480))  # same size as the background image
default_view = "icon-view"
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False
show_icon_preview = False

icon_size = 104
text_size = 13
icon_locations = {
    app_name: (160, 238),
    "Applications": (480, 238),
    # Only visible to users who show hidden files: keep it off the design
    ".background.tiff": (320, 700),
}
