#!/usr/bin/env bash
#
# Regenerates the website screenshots from the real application.
#
# Not a mock: tools/screenshot.cpp builds an actual MainWindow over an actual
# directory and asks Qt to paint it, so the images cannot drift from what the
# program looks like. The results are committed; this is run when the appearance
# changes.
#
# HOME is pointed at a sandbox so the panel headers show a plausible `~/...`
# and the sidebar lists that sandbox's directories rather than yours.

set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

tool="${1:-build/tools/bin/pf-screenshot}"
if [[ ! -x "$tool" ]]; then
    echo "build it first:" >&2
    echo "  cmake -S . -B build/tools -G Ninja -DCMAKE_BUILD_TYPE=Release \\" >&2
    echo "        -DPF_BUILD_TOOLS=ON -DPF_BUILD_TESTS=OFF" >&2
    echo "  cmake --build build/tools --target pf-screenshot" >&2
    exit 1
fi

sandbox="$(mktemp -d)/home"
mkdir -p "$sandbox"/{Desktop,Downloads,Documents,Pictures,Music,Movies}
mkdir -p "$sandbox/Developer/panefile/src"

for dir in app config core fs input model platform ui; do
    mkdir -p "$sandbox/Developer/panefile/src/$dir"
done
printf 'int main(int argc, char **argv) {}\n' > "$sandbox/Developer/panefile/src/main.cpp"
printf 'cmake_minimum_required(VERSION 3.25)\n' > "$sandbox/Developer/panefile/src/CMakeLists.txt"

cd "$sandbox/Downloads"
mkfile -n 740m qt-everywhere-6.10.2.tar.xz
mkfile -n 5m   panefile-1.0.0.zip
mkfile -n 1300k mockup-final.png
mkfile -n 42m  talk-recording.mp4
mkfile -n 890k invoice-2026-08.pdf
printf '#!/bin/sh\necho installing\n' > install.sh && chmod +x install.sh
printf '# Release notes\n' > release-notes.md
ln -sf release-notes.md latest.md
cd - >/dev/null

# Plausible mtimes, so the time column shows a range rather than one minute.
touch -t 202608110914 "$sandbox/Developer/panefile/src/app"
touch -t 202608070031 "$sandbox/Developer/panefile/src/config"
touch -t 202608121847 "$sandbox/Developer/panefile/src/core"
touch -t 202608100728 "$sandbox/Developer/panefile/src/fs"
touch -t 202607281602 "$sandbox/Developer/panefile/src/input"
touch -t 202608092211 "$sandbox/Developer/panefile/src/model"
touch -t 202608031339 "$sandbox/Developer/panefile/src/platform"
touch -t 202608130941 "$sandbox/Developer/panefile/src/ui"
touch -t 202607190845 "$sandbox/Developer/panefile/src/CMakeLists.txt"
touch -t 202608121905 "$sandbox/Developer/panefile/src/main.cpp"
touch -t 202608120732 "$sandbox/Downloads/install.sh"
touch -t 202608010900 "$sandbox/Downloads/invoice-2026-08.pdf"
touch -t 202608111530 "$sandbox/Downloads/release-notes.md"
touch -t 202608111530 "$sandbox/Downloads/latest.md"
touch -t 202608062142 "$sandbox/Downloads/mockup-final.png"
touch -t 202608130903 "$sandbox/Downloads/panefile-1.0.0.zip"
touch -t 202607241118 "$sandbox/Downloads/qt-everywhere-6.10.2.tar.xz"
touch -t 202605300000 "$sandbox/Downloads/talk-recording.mp4"

config="$(mktemp -d)"
for theme in macos-light catppuccin-mocha; do
    HOME="$sandbox" \
    PANEFILE_CONFIG_DIR="$config" \
    PANEFILE_DATA_DIR="$PWD/data" \
    QT_QPA_PLATFORM=offscreen \
        "$tool" \
            --theme "$theme" \
            --paths "$sandbox/Developer/panefile/src,$sandbox/Downloads" \
            --width 1160 --height 430 --scale 2 \
            --output "docs/site/screenshot-$theme.png"
done
