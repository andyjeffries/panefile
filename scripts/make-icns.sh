#!/usr/bin/env bash
#
# Regenerates data/icons/panefile.icns from data/icons/panefile.svg.
#
# The result is committed, so this is only run when the artwork changes. Doing
# it at build time would mean every build — including a Linux CI runner, which
# will never open the file — needing an SVG rasteriser and macOS's iconutil.
#
# Needs rsvg-convert (librsvg) and iconutil (macOS).

set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

for tool in rsvg-convert iconutil; do
    command -v "$tool" >/dev/null || { echo "need $tool" >&2; exit 1; }
done

work="$(mktemp -d)/panefile.iconset"
mkdir -p "$work"

# The ten variants macOS asks for: five sizes, each at 1x and 2x.
render() { rsvg-convert -w "$1" -h "$1" data/icons/panefile.svg -o "$work/$2.png"; }

render 16   icon_16x16
render 32   icon_16x16@2x
render 32   icon_32x32
render 64   icon_32x32@2x
render 128  icon_128x128
render 256  icon_128x128@2x
render 256  icon_256x256
render 512  icon_256x256@2x
render 512  icon_512x512
render 1024 icon_512x512@2x

iconutil -c icns "$work" -o data/icons/panefile.icns
echo "wrote data/icons/panefile.icns"
