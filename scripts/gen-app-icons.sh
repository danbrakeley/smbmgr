#!/usr/bin/env bash
# Regenerates app.ico and the hicolor PNG set from resources/icons/app/app256.png.
# Requires ImageMagick (`magick` on the PATH).
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
app_dir="$script_dir/../resources/icons/app"
src="$app_dir/app256.png"

if [ ! -f "$src" ]; then
    echo "error: source icon not found: $src" >&2
    exit 1
fi

if ! command -v magick >/dev/null 2>&1; then
    echo "error: ImageMagick 'magick' command not found on PATH" >&2
    exit 1
fi

echo "Generating $app_dir/app.ico"
magick "$src" -define icon:auto-resize=256,48,32,16 "$app_dir/app.ico"

hicolor_sizes=(16 32 48 64 128 256)
for size in "${hicolor_sizes[@]}"; do
    out_dir="$app_dir/hicolor/${size}x${size}/apps"
    mkdir -p "$out_dir"
    out="$out_dir/smbmgr.png"
    echo "Generating $out"
    magick "$src" -resize "${size}x${size}" "$out"
done

echo "Done."
