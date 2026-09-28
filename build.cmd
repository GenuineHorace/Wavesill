@echo off
rem Build Wavesill.exe with Visual Studio (run from a "x64 Native Tools Command Prompt").
setlocal
cd /d "%~dp0"
if not exist build mkdir build
rc /nologo /fo build\wavesill.res src\wavesill.rc || exit /b 1
cl /nologo /O2 /std:c++17 /EHsc /W4 /utf-8 /DNDEBUG /DUNICODE /D_UNICODE ^
   src\wavesill.cpp build\wavesill.res /Fe:build\Wavesill.exe /Fo:build\ ^
   /link /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib advapi32.lib dwmapi.lib uxtheme.lib
if errorlevel 1 exit /b 1
echo built build\Wavesill.exe
