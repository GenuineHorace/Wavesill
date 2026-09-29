// Wavesill version — bump here (and in CHANGELOG.md + wavesill.manifest) for every
// revision.  Shared by wavesill.cpp and wavesill.rc so the exe's Properties dialog,
// the tray tooltip and the About window always agree.
#pragma once

#define WS_VER_MAJOR 0
#define WS_VER_MINOR 6
#define WS_VER_PATCH 5

#define WS_STR2(x) #x
#define WS_STR(x)  WS_STR2(x)

#define WS_VERSION_A  WS_STR(WS_VER_MAJOR) "." WS_STR(WS_VER_MINOR) "." WS_STR(WS_VER_PATCH)
#define WS_VERSION_RC WS_VER_MAJOR,WS_VER_MINOR,WS_VER_PATCH,0
#define WS_VERSION_U32 ((WS_VER_MAJOR << 16) | (WS_VER_MINOR << 8) | WS_VER_PATCH)

#define WS_APPNAME_A  "Wavesill"

// Resource IDs
#define IDI_APP        101   // exe / About icon (with backdrop)
#define IDI_TRAY_WHITE 102   // built-in tray glyph for dark taskbars
#define IDI_TRAY_BLACK 103   // built-in tray glyph for light taskbars
