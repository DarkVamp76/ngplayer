#!/bin/sh
# Fetches the SoundFont that ngPlayer plays MIDI files with: GeneralUser GS 2.0.3 by S. Christian Collins (GeneralUser GS License v2.0,
# free to use and to ship in software; the licence text comes with it) into assets/soundfont/ and checks it.
# usage: sh tools/fetch-soundfont.sh        (the AppImage build and 'make install' use what is in assets/soundfont/)
set -e
cd "$(dirname "$0")/.."
D=assets/soundfont; F=$D/GeneralUser-GS.sf2
SUM=9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe
BASE=https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/main
mkdir -p "$D"
if [ -f "$F" ] && [ "$(sha256sum "$F" | cut -d' ' -f1)" = "$SUM" ]; then echo "SoundFont is there: $F"; exit 0; fi
curl -fsSL -o "$F.part" "$BASE/GeneralUser-GS.sf2"
curl -fsSL -o "$D/LICENSE-GeneralUser-GS.txt" "$BASE/documentation/LICENSE.txt"
if [ "$(sha256sum "$F.part" | cut -d' ' -f1)" != "$SUM" ]; then
    echo "the downloaded SoundFont ($F.part) does not match the expected checksum: not used" >&2
    exit 1
fi
mv "$F.part" "$F"
echo "SoundFont fetched: $F"
