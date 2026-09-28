#!/usr/bin/env sh
# Cross-compile Wavesill.exe (Windows x64) from Linux/macOS/Windows with Zig.
#   pip install ziglang     (or install zig from https://ziglang.org)
# Usage: ./build.sh            (ARM64: TARGET=aarch64-windows-gnu ./build.sh)
set -e
cd "$(dirname "$0")"
mkdir -p build
ZIG="${ZIG:-python3 -m ziglang}"
TARGET="${TARGET:-x86_64-windows-gnu}"
STAMP="$(date -u +%Y-%m-%d)"
$ZIG c++ -target "$TARGET" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-nullability-completeness \
  -municode -DWS_BUILD_STAMP="\"$STAMP\"" \
  src/wavesill.cpp src/wavesill.rc -o build/Wavesill.exe \
  -Wl,--subsystem,windows -luser32 -lgdi32 -lshell32 -lole32 -loleaut32 -ladvapi32 -ldwmapi -luxtheme
echo "built build/Wavesill.exe ($TARGET)"
