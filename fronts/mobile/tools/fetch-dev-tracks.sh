#!/bin/bash
# Copies a few tracks into fronts/mobile/media/ (gitignored) for the dev
# build, with their covers and a tracks.json the core reads: title, artist
# and duration from the tags (ffprobe), the embedded cover as a jpg (ffmpeg).
# Usage: fetch-dev-tracks.sh [file.mp3 ...]  (default: three from ~/Music)
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

if [ $# -eq 0 ]; then
    set -- "$HOME/Music/Dj Goja - Mi Chico.mp3" \
        "$HOME/Music/Ed Solo - Acid Dub.mp3" \
        "$HOME/Music/Deep Purple - Soldier Of Fortune.mp3"
fi

rm -rf media && mkdir -p media
entries=()
n=0
for src in "$@"; do
    n=$((n + 1))
    id=$(printf 't%02d' "$n")
    ext="${src##*.}"
    cp "$src" "media/$id.$ext"
    tag() { ffprobe -v error -show_entries "format_tags=$1" -of default=nw=1:nk=1 "$src" | head -1; }
    title=$(tag title)
    artist=$(tag artist)
    # No tags: "Artist - Title.mp3" names the track.
    base=$(basename "$src" ".$ext")
    [ -n "$title" ] || title="${base#* - }"
    [ -n "$artist" ] || artist="${base%% - *}"
    duration=$(ffprobe -v error -show_entries format=duration -of default=nw=1:nk=1 "$src")
    cover=""
    if ffmpeg -v error -y -i "$src" -an -frames:v 1 -vf "scale='min(600,iw)':-2" "media/$id.jpg" 2>/dev/null; then
        cover="$id.jpg"
    fi
    entries+=("$(python3 -c 'import json,sys; print(json.dumps({"id": sys.argv[1], "file": sys.argv[2], "title": sys.argv[3], "artist": sys.argv[4], "durationMs": int(float(sys.argv[5]) * 1000), "cover": sys.argv[6]}, ensure_ascii=False))' \
        "$id" "$id.$ext" "$title" "$artist" "$duration" "$cover")")
done
(IFS=,; echo "[${entries[*]}]") | python3 -m json.tool --no-ensure-ascii > media/tracks.json
echo "media/: $(ls media | tr '\n' ' ')"
