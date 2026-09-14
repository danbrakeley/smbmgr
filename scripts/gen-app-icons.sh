#!/usr/bin/env bash
# Regenerates app.ico and the hicolor PNG set from resources/icons/app/app.svg.
# Requires ImageMagick (`magick` on the PATH) with SVG support.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
app_dir="$script_dir/../resources/icons/app"
src="$app_dir/app.svg"

if [ ! -f "$src" ]; then
    echo "error: source icon not found: $src" >&2
    exit 1
fi

if ! command -v magick >/dev/null 2>&1; then
    echo "error: ImageMagick 'magick' command not found on PATH" >&2
    exit 1
fi

# Rasterize the SVG once, well above the largest output size, with a
# transparent background; every output is downscaled from this master.
master_dir="$(mktemp -d)"
trap 'rm -rf "$master_dir"' EXIT
master="$master_dir/master.png"
magick -background none -density 384 "$src" -resize 1024x1024 "$master"

echo "Generating $app_dir/app.ico"
magick "$master" -define icon:auto-resize=256,48,32,16 "$app_dir/app.ico"

hicolor_sizes=(16 32 48 64 128 256)
for size in "${hicolor_sizes[@]}"; do
    out_dir="$app_dir/hicolor/${size}x${size}/apps"
    mkdir -p "$out_dir"
    out="$out_dir/smbmgr.png"
    echo "Generating $out"
    magick "$master" -resize "${size}x${size}" "$out"
done

echo "Done."
