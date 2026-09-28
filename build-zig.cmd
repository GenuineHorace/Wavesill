@echo off
rem Build Wavesill.exe on Windows with Zig (no Visual Studio needed):  pip install ziglang
setlocal
cd /d "%~dp0"
if not exist build mkdir build
python -m ziglang c++ -target x86_64-windows-gnu -std=c++17 -O2 -Wno-nullability-completeness -municode ^
  src\wavesill.cpp src\wavesill.rc -o build\Wavesill.exe ^
  -Wl,--subsystem,windows -luser32 -lgdi32 -lshell32 -lole32 -loleaut32 -ladvapi32 -ldwmapi -luxtheme
if errorlevel 1 exit /b 1
echo built build\Wavesill.exe
