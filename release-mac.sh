#!/bin/bash
# Mac release: builds a universal (Apple Silicon + Intel) Stinger.app, runs the tests, puts the download
# tools (yt-dlp, ffmpeg, deno) in Stinger.app/Contents/Helpers, signs it ad hoc and makes
# dist/Stinger-<version>-mac.dmg (Stinger.app, an Applications link, the Stream Deck plugin).
# Usage:  ./release-mac.sh          (GitHub Actions runs it on a Mac: .github/workflows/mac.yml)
set -euo pipefail
cd "$(dirname "$0")"

cmake -B build -G Xcode
cmake --build build --config Release --target Stinger EngineTests -- -quiet
build/Release/EngineTests

version=$(sed -nE 's/^project\(Stinger VERSION ([0-9.]+).*/\1/p' CMakeLists.txt)
version+=$(sed -nE 's/^set\(STINGER_VERSION_LABEL "\$\{PROJECT_VERSION\}([^"]*)"\).*/\1/p' CMakeLists.txt)
stage="dist/Stinger-$version-mac"
rm -rf "$stage"
mkdir -p "$stage"
cp -R build/Stinger_artefacts/Release/Stinger.app "$stage/"
app="$stage/Stinger.app"

# Download tools: universal binaries (lipo joins the arm64 and x86_64 builds where there are two).
tools=$(mktemp -d)
helpers="$app/Contents/Helpers"
mkdir -p "$helpers"
fetch() { curl -fsSL --retry 3 -o "$2" "$1"; }
if fetch https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_macos "$helpers/yt-dlp"; then
    chmod +x "$helpers/yt-dlp"
else
    echo "WARNING: couldn't fetch yt-dlp"
fi
for arch in arm64 amd64; do
    mkdir -p "$tools/ffmpeg-$arch"
    fetch "https://ffmpeg.martin-riedl.de/redirect/latest/macos/$arch/release/ffmpeg.zip" "$tools/ffmpeg-$arch.zip" \
        && unzip -q -o "$tools/ffmpeg-$arch.zip" -d "$tools/ffmpeg-$arch" || echo "WARNING: couldn't fetch ffmpeg ($arch)"
done
for arch in aarch64 x86_64; do
    mkdir -p "$tools/deno-$arch"
    fetch "https://github.com/denoland/deno/releases/latest/download/deno-$arch-apple-darwin.zip" "$tools/deno-$arch.zip" \
        && unzip -q -o "$tools/deno-$arch.zip" -d "$tools/deno-$arch" || echo "WARNING: couldn't fetch deno ($arch)"
done
join() {   # join <name> <arm64 file> <x86_64 file>
    if [[ -f "$2" && -f "$3" ]]; then lipo -create "$2" "$3" -output "$helpers/$1"
    elif [[ -f "$2" ]]; then cp "$2" "$helpers/$1"
    elif [[ -f "$3" ]]; then cp "$3" "$helpers/$1"
    else return 0; fi
    chmod +x "$helpers/$1"
}
join ffmpeg "$tools/ffmpeg-arm64/ffmpeg" "$tools/ffmpeg-amd64/ffmpeg"
join deno "$tools/deno-aarch64/deno" "$tools/deno-x86_64/deno"
rm -rf "$tools"
ls -l "$helpers"

# Ad hoc signature (no Apple Developer ID): helpers first, then the app around them.
for f in "$helpers"/*; do codesign --force --sign - "$f"; done
codesign --force --sign - "$app"
codesign --verify --verbose "$app"

# Stream Deck plugin (a zip with the .sdPlugin folder at its root)
(cd streamdeck && zip -qr "../$stage/Stinger.streamDeckPlugin" com.stinger.streamdeck.sdPlugin -x "*.DS_Store")

ln -s /Applications "$stage/Applications"
dmg="dist/Stinger-$version-mac.dmg"
rm -f "$dmg"
hdiutil create -volname "Stinger $version" -srcfolder "$stage" -ov -format UDZO "$dmg"
echo "DMG: $dmg"
