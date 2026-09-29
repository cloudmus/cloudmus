#!/bin/bash
# Renders the installer's banners (art/installer-*.svg, drawn at Modern UI's
# 100% sizes) into 24-bit BMPs — NSIS takes nothing else — one per display
# scale cloudmus.nsi picks from at run time:
#   welcome-<scale>.bmp  164x314 at 100%, the welcome/finish page's side
#   header-<scale>.bmp   150x57 at 100%, the page header's right end
# Usage: make-installer-bitmaps.sh OUT_DIR  (needs rsvg-convert, ImageMagick)
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."

out_dir="${1:?output directory}"
# Keep in step with DPI_SCALES in cloudmus.nsi.
scales=(100 125 150 175 200 250 300)

mkdir -p "${out_dir}"
for name in welcome header; do
    svg="art/installer-${name}.svg"
    for scale in "${scales[@]}"; do
        rsvg-convert --zoom "$(echo "${scale}" | awk '{print $1 / 100}')" "${svg}" \
            | convert png:- -background white -flatten -type TrueColor \
                "BMP3:${out_dir}/${name}-${scale}.bmp"
    done
done
