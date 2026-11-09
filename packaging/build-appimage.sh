#!/bin/sh
# Baut ngPlayer-<version>-x86_64.AppImage. Benoetigt: gcc, make, SDL2-Entwicklungspakete, curl.
# Aufruf aus dem Repo-Wurzelverzeichnis: sh packaging/build-appimage.sh [version]
# Umgebung: SDL_CFLAGS/SDL_LIBS (optional, sonst sdl2-config), LINUXDEPLOY (Pfad zu linuxdeploy, sonst Download)
set -e
cd "$(dirname "$0")/.."
VERSION="${1:-$(git describe --tags --always 2>/dev/null || echo dev)}"
OUT="$PWD/dist"; WORK="$PWD/build/appimage"
make clean >/dev/null
make build/ngplayer
rm -rf "$WORK"; mkdir -p "$WORK" "$OUT"

if [ -z "$LINUXDEPLOY" ]; then
    LINUXDEPLOY="$WORK/linuxdeploy.AppImage"
    curl -fsSL -o "$LINUXDEPLOY" https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
    chmod +x "$LINUXDEPLOY"
fi

cp assets/ngplayer-256.png "$WORK/ngplayer.png"
cp assets/ngplayer.svg "$WORK/ngplayer.svg"
cd "$WORK"
export VERSION
export LDAI_OUTPUT="$OUT/ngPlayer-$VERSION-x86_64.AppImage"
export APPIMAGE_EXTRACT_AND_RUN=1
"$LINUXDEPLOY" --appdir AppDir \
    --executable ../ngplayer \
    --desktop-file ../../packaging/ngplayer.desktop \
    --icon-file ngplayer.png --icon-file ngplayer.svg \
    --output appimage
ls -la "$OUT"
