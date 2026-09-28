#!/usr/bin/env sh
# Builds both architectures with Zig and stages a release folder in dist/.
set -e
cd "$(dirname "$0")"
VER=$(sed -n 's/#define WS_VER_MAJOR \([0-9]*\)/\1/p' src/version.h).$(sed -n 's/#define WS_VER_MINOR \([0-9]*\)/\1/p' src/version.h).$(sed -n 's/#define WS_VER_PATCH \([0-9]*\)/\1/p' src/version.h)
rm -rf dist && mkdir -p "dist/Wavesill-v$VER"
TARGET=x86_64-windows-gnu ./build.sh && cp build/Wavesill.exe "dist/Wavesill-v$VER/Wavesill.exe"
TARGET=aarch64-windows-gnu ./build.sh && cp build/Wavesill.exe "dist/Wavesill-v$VER/Wavesill-arm64.exe"
cp README.md CHANGELOG.md BRIDGE.md MOD-GUIDE.md LICENSE "dist/Wavesill-v$VER/"
cp mod/wavesill-behind-taskbar-content.wh.cpp "dist/Wavesill-v$VER/"
cp "dist/Wavesill-v$VER/Wavesill.exe" "dist/Wavesill-v$VER/Wavesill-arm64.exe" mod/wavesill-behind-taskbar-content.wh.cpp dist/
(cd dist && zip -qr "Wavesill-v$VER.zip" "Wavesill-v$VER")
echo "dist/Wavesill-v$VER.zip"
