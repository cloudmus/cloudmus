#!/bin/bash
# Renders the app's Android icons from art/ into fronts/mobile/android/res
# (committed: inkscape isn't on every build machine). Run again when the
# logo changes.
#   ic_launcher, ic_launcher_round   art/logo.svg — the round logo, pre-8 launchers
#   ic_launcher_foreground           art/small_logo.svg — the cloud, 8+ adaptive
#                                    icon's front layer (its back: the logo's
#                                    gradient, drawable/ic_launcher_background.xml)
#   ic_stat_cloudmus                 the cloud as a white silhouette: the
#                                    notification's small icon
# Needs inkscape and ImageMagick.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../.."
res=fronts/mobile/android/res
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

render() { # svg size out
    inkscape "$1" --export-type=png --export-filename="$3" -w "$2" -h "$2" > /dev/null 2>&1
}

rm -f "$res"/mipmap-*/ic_launcher*.png "$res"/drawable-*/ic_stat_cloudmus.png
# density → scale of a dp
for density in mdpi:1 hdpi:1.5 xhdpi:2 xxhdpi:3 xxxhdpi:4; do
    name=${density%%:*}
    scale=${density#*:}
    px() { awk -v dp="$1" -v s="$scale" 'BEGIN { printf "%d", dp * s + 0.5 }'; }
    mkdir -p "$res/mipmap-$name" "$res/drawable-$name"

    render art/logo.svg "$(px 48)" "$res/mipmap-$name/ic_launcher.png"
    cp "$res/mipmap-$name/ic_launcher.png" "$res/mipmap-$name/ic_launcher_round.png"

    # 108 dp layer; launchers mask it down to a 72 dp view and promise only
    # the central 66 dp: the cloud goes in 48 dp.
    render art/small_logo.svg "$(px 48)" "$tmp/cloud.png"
    magick "$tmp/cloud.png" -background none -gravity center -extent "$(px 108)x$(px 108)" \
        "$res/mipmap-$name/ic_launcher_foreground.png"

    # Status bar icons are 24 dp with the drawing in the inner 22.
    render art/small_logo.svg "$(px 22)" "$tmp/stat.png"
    magick "$tmp/stat.png" -alpha extract -background white -alpha shape -background none -gravity center \
        -extent "$(px 24)x$(px 24)" "$res/drawable-$name/ic_stat_cloudmus.png"
done
ls "$res"/mipmap-* "$res"/drawable-* | head -40
