// =====================================================================================
//  Wavesill (栏声) — a faint audio spectrum that lives on the Windows taskbar.
//
//  Single-file Win32/C++17 program, no dependencies beyond the Windows SDK.
//
//  How it works
//  ------------
//  * Audio   : WASAPI loopback on the default render device (whatever Windows is
//              playing right now).  A capture thread converts to mono float and pushes
//              into a small ring buffer.  Device changes / errors → automatic re-open.
//  * Analysis: 2048-point Hann-windowed FFT on the UI thread, ~60 fps.  Bins are mapped
//              onto log-spaced bands (35 Hz … 16 kHz).  A slow automatic gain keeps quiet
//              notifications and loud music both readable.  Optional peak-hold.
//  * Display : one layered (per-pixel alpha) window per taskbar window
//              (Shell_TrayWnd + every Shell_SecondaryTrayWnd).  It is click-through,
//              never activates, always topmost, and re-asserts its z-order so the
//              taskbar cannot climb above it.  Three styles with an optional fade,
//              three colours, per-monitor overrides.
//  * Bridge  : if a Windhawk mod that speaks wavesill_bridge.h is loaded, finished
//              frames are handed to it through shared memory and it shows them inside
//              the taskbar itself; the overlay hides for that taskbar while the mod's
//              heartbeat is fresh and comes back the moment it stops.
//  * Geometry: every frame we read the taskbar's real on-screen rect + DPI + edge and
//              follow it.  That single rule covers resolution changes, DPI changes,
//              taskbar moved to another edge, auto-hide sliding, Explorer restarts.
//              On Windows 11 the taskbar's 1-DIP top border is left untouched.
//  * Data    : %LOCALAPPDATA%\Wavesill\settings.ini (all settings), TrayIcons\ (optional
//              user-supplied tray icons).  Only the "Start with Windows" entry touches
//              the registry, because Windows keeps that list there.
//  * Theme   : SystemUsesLightTheme drives the tray glyph, the menu and the About
//              window; the spectrum follows it too unless a colour is chosen.
//
//  Version: see version.h (Properties → Details of the exe shows the same number).
//  Project: https://github.com/GenuineHorace/Wavesill — MIT License.
// =====================================================================================

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <objbase.h>
#include <propidl.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <d2d1.h>
#include <dwrite.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "version.h"
#include "wavesill_bridge.h"

#if defined(_MSC_VER)
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#endif

#define WS_WIDE2(x) L##x
#define WS_WIDE(x)  WS_WIDE2(x)
static const wchar_t* const WS_VERSION_W = WS_WIDE(WS_VERSION_A);
#ifndef WS_BUILD_STAMP
#define WS_BUILD_STAMP __DATE__   // build scripts pass -DWS_BUILD_STAMP="\"2026-09-28\""
#endif

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif
#ifndef NIF_SHOWTIP
#define NIF_SHOWTIP 0x00000080
#endif
#ifndef NOTIFYICON_VERSION_4
#define NOTIFYICON_VERSION_4 4
#endif
#ifndef NIN_SELECT
#define NIN_SELECT (WM_USER + 0)
#endif
#ifndef NIN_KEYSELECT
#define NIN_KEYSELECT (WM_USER + 1)
#endif
#ifndef AUDCLNT_STREAMFLAGS_LOOPBACK
#define AUDCLNT_STREAMFLAGS_LOOPBACK 0x00020000
#endif
#ifndef AUDCLNT_BUFFERFLAGS_SILENT
#define AUDCLNT_BUFFERFLAGS_SILENT 0x2
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif

// --------------------------------------------------------------------------- GUIDs
// Spelled out so the file builds identically with MSVC, clang and MinGW.
static const CLSID WS_CLSID_MMDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
static const IID   WS_IID_IMMDeviceEnumerator  = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
static const IID   WS_IID_IMMNotificationClient= {0x7991EEC9, 0x7E89, 0x4D85, {0x83, 0x90, 0x6C, 0x70, 0x3C, 0xEC, 0x60, 0xC0}};
static const IID   WS_IID_IAudioClient         = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
static const IID   WS_IID_IAudioCaptureClient  = {0xC8ADBD64, 0xE71E, 0x48A0, {0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17}};
static const IID   WS_IID_IUnknown             = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID  WS_SUBTYPE_IEEE_FLOAT       = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
static const GUID  WS_SUBTYPE_PCM              = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
static const PROPERTYKEY WS_PKEY_Device_FriendlyName = {{0xA45C254E, 0xDF1C, 0x4EFD, {0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0}}, 14};

// --------------------------------------------------------------------------- tuning
namespace cfg {
constexpr int   FFT_N        = 2048;     // ~43 ms window at 48 kHz, ~23 Hz per bin
constexpr float FREQ_MIN     = 35.f;     // leftmost band
constexpr float FREQ_MAX     = 16000.f;  // rightmost band (clamped to 0.45 * sample rate)
constexpr float RANGE_DB     = 50.f;     // dynamic range drawn below the current ceiling
constexpr float CEIL_MIN_DB  = -42.f;    // auto-gain never lifts the ceiling below this
constexpr float CEIL_RELEASE = 8.f;      // dB/s — how fast the ceiling falls after loud audio
constexpr float GATE_DB      = -78.f;    // absolutely nothing is drawn below this level
constexpr float TILT_DB_OCT  = 4.f;      // treble tilt (music energy falls with frequency)
constexpr float TILT_FROM_HZ = 250.f;
constexpr float TILT_MAX_DB  = 20.f;
constexpr float ATTACK_S     = 0.030f;   // level rise time constant
constexpr float RELEASE_S    = 0.140f;   // level fall time constant
constexpr float PEAK_HOLD_S  = 0.30f;    // peak-hold: how long a peak stays before falling
constexpr float PEAK_GRAVITY = 3.0f;     // peak-hold: fall acceleration, full-heights per s²
constexpr int   BAR_PITCH    = 4;        // bar + gap, px at 96 dpi
constexpr int   BAR_GAP      = 1;        // px at 96 dpi
constexpr float LINE_W       = 1.5f;     // curve width, px at 96 dpi
constexpr float PEAK_LINE_W  = 1.0f;     // peak-hold curve width, px at 96 dpi
constexpr float CAP_H        = 1.5f;     // peak-hold cap on bars, px at 96 dpi
constexpr int   CURVE_SS     = 4;        // curve styles: horizontal sub-samples per pixel column
constexpr float FADE_FLOOR   = 0.30f;    // "+ fade" styles: opacity left at the root edge (of the top)
constexpr int   FRAME_MS     = 16;       // ~60 fps
constexpr int   TASKBAR_SCAN_MS = 1000;  // how often to look for new/removed taskbars
constexpr int   ZORDER_EVERY_N  = 15;    // frames between z-order checks
// Opacity steps shown in the menu, in percent.  Index 2 (10 %) is the default.
const int   OPACITY_PCT[] = {4, 7, 10, 14, 20, 30};
constexpr int OPACITY_COUNT = 6;
constexpr int OPACITY_DEFAULT = 2;
}  // namespace cfg

enum Style { ST_BARS = 0, ST_FILLED, ST_LINE, ST_COUNT };
enum Fall  { FALL_INSTANT = 0, FALL_PEAK_HOLD, FALL_COUNT };
enum Color { COL_WHITE = 0, COL_BLACK, COL_THEME, COL_COUNT };

// --------------------------------------------------------------------------- strings
enum StrId {
    S_APPNAME,
    S_STYLE, S_ST_BARS, S_ST_FILLED, S_ST_LINE, S_FADE,
    S_COLOR, S_COL_WHITE, S_COL_BLACK, S_COL_THEME,
    S_OPACITY, S_DEFAULT_TAG,
    S_FALL, S_FALL_INSTANT, S_FALL_PEAK,
    S_PER_MONITOR, S_OVERRIDE, S_SHOW_HERE, S_PRIMARY,
    S_EDGE_BOTTOM, S_EDGE_TOP, S_EDGE_LEFT, S_EDGE_RIGHT,
    S_CUSTOM_TRAY, S_OPEN_FOLDER,
    S_PAUSE, S_AUTOSTART, S_ABOUT, S_EXIT,
    S_ABOUT_TITLE, S_TAGLINE,
    S_L_DEVICE, S_L_RATE, S_L_THEME, S_L_TRAY, S_L_MOD, S_L_TASKBARS, S_L_SETTINGS, S_L_BUILD, S_EMULATED, S_L_PROJECT,
    S_NO_DEVICE, S_THEME_DARK, S_THEME_LIGHT, S_TRAY_BUILTIN, S_TRAY_CUSTOM,
    S_MOD_NONE, S_MOD_CONNECTED, S_MOD_INCOMPAT, S_MOD_STALE, S_L_STAGE, S_STAGE_WAIT,
    S_STAGE_0, S_STAGE_1, S_STAGE_2, S_STAGE_3, S_STAGE_4, S_STAGE_5, S_STAGE_6, S_STAGE_7, S_STAGE_8, S_STAGE_GUARD, S_STAGE_OFF,
    S_VIA_OVERLAY, S_VIA_MOD, S_VIA_HIDDEN, S_VIA_OFF,
    S_BTN_OK,
    S_COUNT
};
static const wchar_t* STR_EN[S_COUNT] = {
    L"Wavesill",
    L"Style", L"Bars", L"Filled", L"Line", L"Fade Toward the Edge",
    L"Color", L"White", L"Black", L"Follow Theme",
    L"Opacity", L" (Default)",
    L"Fall", L"Instant", L"Peak Hold",
    L"Per Monitor", L"Use Its Own Settings", L"Show on This Taskbar", L"Primary",
    L"Bottom", L"Top", L"Left", L"Right",
    L"Custom Tray Icon", L"Open Data Folder",
    L"Pause", L"Start with Windows", L"About\u2026", L"Exit",
    L"About Wavesill", L"A faint audio spectrum that lives on the taskbar.",
    L"Audio Device:", L"Sample Rate:", L"System Theme:", L"Tray Icon:", L"Windhawk Mod:", L"Taskbars:", L"Settings:", L"Build:", L"emulated on", L"Project:",
    L"None (waiting for an output device)", L"Dark", L"Light", L"Built-in", L"Custom",
    L"Not detected", L"Connected", L"Incompatible", L"Stale", L"Mod Stage:", L"Waiting for the taskbar",
    L"Symbols resolved", L"Taskbar thread reached", L"Timer created", L"Ticking", L"XAML root found", L"Background element found", L"Element inserted", L"Taskbar matched", L"Frames flowing",
    L"Skipped: The previous attach crashed Explorer (re-enable the mod to retry)", L"Attach disabled in the mod's settings",
    L"Overlay", L"Mod", L"Hidden", L"Off",
    L"OK",
};
static const wchar_t* STR_ZH[S_COUNT] = {
    L"栏声",
    L"样式", L"柱条", L"填充", L"曲线", L"向边缘渐淡",
    L"颜色", L"白色", L"黑色", L"跟随主题",
    L"浓度", L" (默认)",
    L"下落", L"即时", L"峰值保持",
    L"各显示器", L"单独设置 (不跟随全局)", L"在此任务栏显示", L"主显示器",
    L"底部", L"顶部", L"左侧", L"右侧",
    L"自定义托盘图标", L"打开数据文件夹",
    L"暂停", L"开机自动启动", L"关于…", L"退出",
    L"关于栏声", L"融在任务栏里、不挡操作的淡淡频谱。",
    L"音频设备：", L"采样率：", L"系统主题：", L"托盘图标：", L"Windhawk 模组：", L"任务栏：", L"设置文件：", L"构建：", L"模拟运行于", L"项目：",
    L"无 (等待输出设备)", L"深色", L"浅色", L"内置", L"自定义",
    L"未检测到", L"已连接", L"不兼容", L"无响应", L"模组阶段：", L"等待任务栏",
    L"符号已解析", L"已到达任务栏线程", L"定时器已建", L"已在跳动", L"找到 XAML 根", L"找到背景元素", L"已插入元素", L"已匹配任务栏", L"帧已送达",
    L"已跳过：上次接入时 Explorer 崩溃 (重新启用模组可再试一次)", L"模组设置里关闭了接入",
    L"覆盖层", L"模组", L"已隐藏", L"关闭",
    L"确定",
};
static bool g_zh = false;
static const wchar_t* S(StrId id) { return (g_zh ? STR_ZH : STR_EN)[id]; }

// --------------------------------------------------------------------------- small helpers
static DWORD g_build = 0;
static bool  g_win11 = false;

static DWORD windowsBuild() {
    typedef LONG (WINAPI* RtlGetVersionFn)(PRTL_OSVERSIONINFOW);
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    auto fn = nt ? (RtlGetVersionFn)GetProcAddress(nt, "RtlGetVersion") : nullptr;
    RTL_OSVERSIONINFOW vi{}; vi.dwOSVersionInfoSize = sizeof vi;
    if (fn && fn(&vi) == 0) return vi.dwBuildNumber;
    return 0;
}
static bool fileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static bool dirExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
static void writeUtf16File(const std::wstring& path, const std::wstring& text) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    const wchar_t bom = 0xFEFF; DWORD n;
    WriteFile(f, &bom, 2, &n, nullptr);
    WriteFile(f, text.c_str(), (DWORD)(text.size() * 2), &n, nullptr);
    CloseHandle(f);
}

// --------------------------------------------------------------------------- data folder
static std::wstring g_dataDir, g_iniPath, g_trayDir;

static void initDataFolder() {
    wchar_t buf[MAX_PATH] = L"";
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, buf)) || !buf[0]) {
        GetModuleFileNameW(nullptr, buf, MAX_PATH);           // last resort: next to the exe
        wchar_t* p = wcsrchr(buf, L'\\'); if (p) *p = 0;
    }
    g_dataDir = std::wstring(buf) + L"\\Wavesill";
    g_iniPath = g_dataDir + L"\\settings.ini";
    g_trayDir = g_dataDir + L"\\TrayIcons";
    CreateDirectoryW(g_dataDir.c_str(), nullptr);
    if (!dirExists(g_trayDir)) {
        CreateDirectoryW(g_trayDir.c_str(), nullptr);
        writeUtf16File(g_trayDir + L"\\README.txt",
            L"Wavesill \u2014 Custom Tray Icons / \u81ea\u5b9a\u4e49\u6258\u76d8\u56fe\u6807\r\n"
            L"\r\n"
            L"Put .ico files here and Wavesill will use them instead of the built-in glyph:\r\n"
            L"\u628a .ico \u6587\u4ef6\u653e\u5728\u8fd9\u91cc\uff0c\u680f\u58f0\u5c31\u4f1a\u7528\u5b83\u4eec\u4ee3\u66ff\u5185\u7f6e\u56fe\u6807\uff1a\r\n"
            L"\r\n"
            L"  tray_dark.ico    Used when the taskbar is dark  / \u6df1\u8272\u4efb\u52a1\u680f\u65f6\u4f7f\u7528\r\n"
            L"  tray_light.ico   Used when the taskbar is light / \u6d45\u8272\u4efb\u52a1\u680f\u65f6\u4f7f\u7528\r\n"
            L"  tray.ico         Used for both when the matching file above is missing / \u4e24\u79cd\u4e3b\u9898\u5171\u7528\r\n"
            L"\r\n"
            L"Include 16, 20, 24 and 32 px images in the .ico for crisp results at every DPI.\r\n"
            L"A \"Custom Tray Icon\" item appears in the tray menu once a file is found.\r\n"
            L".ico \u91cc\u6700\u597d\u5305\u542b 16\u300120\u300124\u300132 px \u51e0\u79cd\u5c3a\u5bf8\u3002\u68c0\u6d4b\u5230\u6587\u4ef6\u540e\u6258\u76d8\u83dc\u5355\u4f1a\u591a\u51fa\u201c\u81ea\u5b9a\u4e49\u6258\u76d8\u56fe\u6807\u201d\u4e00\u9879\u3002\r\n");
    }
    if (!fileExists(g_iniPath)) {
        writeUtf16File(g_iniPath,
            L"; Wavesill settings.  Edited by the tray menu; you can also edit by hand while\r\n"
            L"; Wavesill is not running.  Delete this file to return to defaults.\r\n"
            L"; \u680f\u58f0\u8bbe\u7f6e\u3002\u6258\u76d8\u83dc\u5355\u4f1a\u6539\u5199\u6b64\u6587\u4ef6\uff1b\u4e5f\u53ef\u4ee5\u5728\u680f\u58f0\u672a\u8fd0\u884c\u65f6\u624b\u5de5\u7f16\u8f91\u3002\u5220\u6389\u6b64\u6587\u4ef6\u5373\u6062\u590d\u9ed8\u8ba4\u3002\r\n"
            L";\r\n"
            L"; Style: 0 Bars, 1 Filled, 2 Line        Fade: 0 Solid, 1 Fade toward the taskbar edge\r\n"
            L"; Color: 0 White, 1 Black, 2 Follow Theme      Fall: 0 Instant, 1 Peak Hold\r\n"
            L"; Opacity: Index 0..5 = 4, 7, 10, 14, 20, 30 percent\r\n"
            L"; BorderInset: \"auto\" (1 px on Windows 11, 0 on Windows 10) or a pixel count at 96 DPI\r\n"
            L"\r\n"
            L"[Wavesill]\r\n"
            L"Style=0\r\nFade=0\r\nColor=2\r\nOpacity=2\r\nFall=0\r\n"
            L"CustomTrayIcon=1\r\nBorderInset=auto\r\n");
    }
}

// --------------------------------------------------------------------------- settings (ini)
struct Visual { int style = ST_BARS; bool fade = false; int opacity = cfg::OPACITY_DEFAULT; int fall = FALL_INSTANT; int color = COL_THEME; };
struct MonitorPrefs { bool override_ = false; bool enabled = true; Visual v; };
struct Settings {
    Visual global;
    bool paused = false;
    bool customTray = true;
    int  borderInset = -1;   // -1 = auto
    std::map<std::wstring, MonitorPrefs> monitors;   // keyed by display device name (\\.\DISPLAY1)
};
static Settings g_settings;

static int iniInt(const wchar_t* sec, const wchar_t* key, int def) {
    return (int)GetPrivateProfileIntW(sec, key, def, g_iniPath.c_str());
}
static std::wstring iniStr(const wchar_t* sec, const wchar_t* key, const wchar_t* def) {
    wchar_t buf[256]; GetPrivateProfileStringW(sec, key, def, buf, 256, g_iniPath.c_str()); return buf;
}
static void iniSet(const wchar_t* sec, const wchar_t* key, int v) {
    wchar_t buf[32]; _snwprintf_s(buf, 32, _TRUNCATE, L"%d", v);
    WritePrivateProfileStringW(sec, key, buf, g_iniPath.c_str());
}
static void clampVisual(Visual& v) {
    v.style = std::min(std::max(v.style, 0), ST_COUNT - 1);
    v.opacity = std::min(std::max(v.opacity, 0), cfg::OPACITY_COUNT - 1);
    v.fall = std::min(std::max(v.fall, 0), FALL_COUNT - 1);
    v.color = std::min(std::max(v.color, 0), COL_COUNT - 1);
}
static void visualLoad(const wchar_t* sec, Visual& v) {
    v.style = iniInt(sec, L"Style", v.style);
    v.opacity = iniInt(sec, L"Opacity", v.opacity);
    v.fall = iniInt(sec, L"Fall", v.fall);
    v.color = iniInt(sec, L"Color", v.color);
    v.fade = iniInt(sec, L"Fade", v.fade ? 1 : 0) != 0;
    clampVisual(v);
}
static void visualSave(const wchar_t* sec, const Visual& v) {
    iniSet(sec, L"Style", v.style); iniSet(sec, L"Opacity", v.opacity);
    iniSet(sec, L"Fall", v.fall);   iniSet(sec, L"Color", v.color); iniSet(sec, L"Fade", v.fade ? 1 : 0);
}
static const wchar_t* INI_MAIN = L"Wavesill";
static void settingsLoad() {
    visualLoad(INI_MAIN, g_settings.global);
    g_settings.customTray = iniInt(INI_MAIN, L"CustomTrayIcon", 1) != 0;
    std::wstring bi = iniStr(INI_MAIN, L"BorderInset", L"auto");
    g_settings.borderInset = (bi.empty() || bi[0] == L'a' || bi[0] == L'A') ? -1 : std::min(std::max(_wtoi(bi.c_str()), 0), 8);
}
static void settingsSaveGlobal() {
    visualSave(INI_MAIN, g_settings.global);
    iniSet(INI_MAIN, L"CustomTrayIcon", g_settings.customTray ? 1 : 0);
}
static std::wstring monSection(const std::wstring& dev) { return L"Monitor " + dev; }
static MonitorPrefs& monitorPrefs(const std::wstring& dev) {
    auto it = g_settings.monitors.find(dev);
    if (it != g_settings.monitors.end()) return it->second;
    MonitorPrefs p;
    p.v = g_settings.global;
    std::wstring sec = monSection(dev);
    p.override_ = iniInt(sec.c_str(), L"Override", 0) != 0;
    p.enabled = iniInt(sec.c_str(), L"Enabled", 1) != 0;
    visualLoad(sec.c_str(), p.v);
    return g_settings.monitors[dev] = p;
}
static void monitorPrefsSave(const std::wstring& dev) {
    const MonitorPrefs& p = monitorPrefs(dev);
    std::wstring sec = monSection(dev);
    iniSet(sec.c_str(), L"Override", p.override_ ? 1 : 0);
    iniSet(sec.c_str(), L"Enabled", p.enabled ? 1 : 0);
    visualSave(sec.c_str(), p.v);
}

// "Start with Windows" is the one thing that must live in the registry.
static const wchar_t* REG_RUN = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static bool autostartEnabled() {
    HKEY k; bool on = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN, 0, KEY_READ, &k) == ERROR_SUCCESS) {
        on = RegQueryValueExW(k, L"Wavesill", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
        RegCloseKey(k);
    }
    return on;
}
static void autostartSet(bool on) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_RUN, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    if (on) {
        wchar_t path[MAX_PATH + 2]; GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring cmd = L"\"" + std::wstring(path) + L"\"";
        RegSetValueExW(k, L"Wavesill", 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, L"Wavesill");
    }
    RegCloseKey(k);
}
static bool readLightTheme() {
    HKEY k; DWORD v = 0, sz = sizeof v, type = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    if (RegQueryValueExW(k, L"SystemUsesLightTheme", nullptr, &type, (LPBYTE)&v, &sz) != ERROR_SUCCESS || type != REG_DWORD) v = 0;
    RegCloseKey(k);
    return v == 1;
}

// --------------------------------------------------------------------------- ring buffer
class SampleRing {
public:
    SampleRing() { InitializeCriticalSection(&cs_); buf_.assign(CAP, 0.f); }
    ~SampleRing() { DeleteCriticalSection(&cs_); }
    void push(const float* s, size_t n) {
        if (n >= CAP) { s += n - CAP; n = CAP; }
        EnterCriticalSection(&cs_);
        for (size_t i = 0; i < n; ++i) { buf_[w_] = s[i]; if (++w_ == CAP) w_ = 0; }
        LeaveCriticalSection(&cs_);
    }
    void pushZeros(size_t n) {
        if (n > CAP) n = CAP;
        EnterCriticalSection(&cs_);
        for (size_t i = 0; i < n; ++i) { buf_[w_] = 0.f; if (++w_ == CAP) w_ = 0; }
        LeaveCriticalSection(&cs_);
    }
    void latest(float* out, size_t n) {   // most recent n samples, oldest first
        if (n > CAP) n = CAP;
        EnterCriticalSection(&cs_);
        size_t p = (w_ + CAP - n) % CAP;
        for (size_t i = 0; i < n; ++i) { out[i] = buf_[p]; if (++p == CAP) p = 0; }
        LeaveCriticalSection(&cs_);
    }
private:
    static constexpr size_t CAP = 16384;
    std::vector<float> buf_;
    size_t w_ = 0;
    CRITICAL_SECTION cs_;
};
static SampleRing g_ring;

// --------------------------------------------------------------------------- audio capture
class NotifClient final : public IMMNotificationClient {
public:
    explicit NotifClient(std::atomic<bool>* flag) : flag_(flag) {}
    STDMETHODIMP QueryInterface(REFIID riid, void** pp) override {
        if (!pp) return E_POINTER;
        if (IsEqualIID(riid, WS_IID_IUnknown) || IsEqualIID(riid, WS_IID_IMMNotificationClient)) {
            *pp = static_cast<IMMNotificationClient*>(this); AddRef(); return S_OK;
        }
        *pp = nullptr; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return (ULONG)InterlockedIncrement(&ref_); }
    STDMETHODIMP_(ULONG) Release() override { return (ULONG)InterlockedDecrement(&ref_); }
    STDMETHODIMP OnDeviceStateChanged(LPCWSTR, DWORD) override { flag_->store(true); return S_OK; }
    STDMETHODIMP OnDeviceAdded(LPCWSTR) override { return S_OK; }
    STDMETHODIMP OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    STDMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
        if (flow == eRender && role == eConsole) flag_->store(true);
        return S_OK;
    }
    STDMETHODIMP OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }
private:
    std::atomic<bool>* flag_;
    LONG ref_ = 1;
};

class LoopbackCapture {
public:
    LoopbackCapture() { InitializeCriticalSection(&cs_); }
    ~LoopbackCapture() { stop(); DeleteCriticalSection(&cs_); }
    void start() {
        stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        thread_ = CreateThread(nullptr, 0, &LoopbackCapture::entry, this, 0, nullptr);
    }
    void stop() {
        if (!thread_) return;
        SetEvent(stopEvent_);
        WaitForSingleObject(thread_, 4000);
        CloseHandle(thread_); CloseHandle(stopEvent_);
        thread_ = nullptr; stopEvent_ = nullptr;
    }
    int  sampleRate() const { return sampleRate_.load(); }
    std::wstring deviceName() { EnterCriticalSection(&cs_); std::wstring s = name_; LeaveCriticalSection(&cs_); return s; }

private:
    enum Kind { K_NONE, K_F32, K_I16, K_I24, K_I32 };
    static DWORD WINAPI entry(LPVOID p) { static_cast<LoopbackCapture*>(p)->run(); return 0; }
    bool stopping(DWORD waitMs = 0) { return WaitForSingleObject(stopEvent_, waitMs) != WAIT_TIMEOUT; }

    void run() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        IMMDeviceEnumerator* en = nullptr;
        CoCreateInstance(WS_CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, WS_IID_IMMDeviceEnumerator, (void**)&en);
        NotifClient notif(&deviceChanged_);
        if (en) en->RegisterEndpointNotificationCallback(&notif);
        while (!stopping()) {
            deviceChanged_.store(false);
            bool ok = en && session(en);
            if (stopping(ok ? 250 : 1500)) break;
        }
        if (en) { en->UnregisterEndpointNotificationCallback(&notif); en->Release(); }
        CoUninitialize();
    }
    void setName(const wchar_t* s) { EnterCriticalSection(&cs_); name_ = s ? s : L""; LeaveCriticalSection(&cs_); }

    bool session(IMMDeviceEnumerator* en) {
        IMMDevice* dev = nullptr;
        if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) || !dev) { setName(L""); return false; }
        IPropertyStore* ps = nullptr;
        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &ps)) && ps) {
            PROPVARIANT v; PropVariantInit(&v);
            if (SUCCEEDED(ps->GetValue(WS_PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR) setName(v.pwszVal);
            PropVariantClear(&v);
            ps->Release();
        }
        IAudioClient* client = nullptr; IAudioCaptureClient* cap = nullptr; WAVEFORMATEX* fmt = nullptr;
        bool opened = false;
        do {
            if (FAILED(dev->Activate(WS_IID_IAudioClient, CLSCTX_ALL, nullptr, (void**)&client)) || !client) break;
            if (FAILED(client->GetMixFormat(&fmt)) || !fmt) break;
            if (!describe(fmt)) break;
            if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 2000000, 0, fmt, nullptr))) break;
            if (FAILED(client->GetService(WS_IID_IAudioCaptureClient, (void**)&cap)) || !cap) break;
            if (FAILED(client->Start())) break;
            opened = true;
        } while (false);
        if (opened) { sampleRate_.store((int)fmt->nSamplesPerSec); pump(cap); client->Stop(); }
        if (cap) cap->Release();
        if (fmt) CoTaskMemFree(fmt);
        if (client) client->Release();
        dev->Release();
        return opened;
    }
    bool describe(const WAVEFORMATEX* fmt) {
        WORD tag = fmt->wFormatTag;
        if (tag == WAVE_FORMAT_EXTENSIBLE && fmt->cbSize >= 22) {
            const auto* ex = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(fmt);
            if (IsEqualGUID(ex->SubFormat, WS_SUBTYPE_IEEE_FLOAT)) tag = WAVE_FORMAT_IEEE_FLOAT;
            else if (IsEqualGUID(ex->SubFormat, WS_SUBTYPE_PCM)) tag = WAVE_FORMAT_PCM;
        }
        channels_ = fmt->nChannels ? fmt->nChannels : 1;
        int bytes = fmt->nBlockAlign / channels_;
        kind_ = K_NONE;
        if (tag == WAVE_FORMAT_IEEE_FLOAT && bytes == 4) kind_ = K_F32;
        else if (tag == WAVE_FORMAT_PCM && bytes == 2) kind_ = K_I16;
        else if (tag == WAVE_FORMAT_PCM && bytes == 3) kind_ = K_I24;
        else if (tag == WAVE_FORMAT_PCM && bytes == 4) kind_ = K_I32;
        return kind_ != K_NONE;
    }
    void pump(IAudioCaptureClient* cap) {
        const int sr = sampleRate_.load();
        ULONGLONG lastData = GetTickCount64(), lastPoll = lastData;
        while (!stopping(10)) {
            if (deviceChanged_.load()) break;
            const ULONGLONG now = GetTickCount64();
            UINT32 pkt = 0;
            if (FAILED(cap->GetNextPacketSize(&pkt))) break;
            if (pkt == 0) {
                // WASAPI delivers nothing while the system is silent; after a short grace
                // period feed zeros in real time so the picture decays instead of freezing.
                if (now - lastData > 60) {
                    ULONGLONG ms = std::min<ULONGLONG>(now - lastPoll, 100);
                    g_ring.pushZeros((size_t)(sr * ms / 1000));
                }
                lastPoll = now;
                continue;
            }
            bool fail = false;
            while (pkt > 0) {
                BYTE* data = nullptr; UINT32 frames = 0; DWORD flags = 0;
                if (FAILED(cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) { fail = true; break; }
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) g_ring.pushZeros(frames); else convert(data, frames);
                cap->ReleaseBuffer(frames);
                if (FAILED(cap->GetNextPacketSize(&pkt))) { fail = true; break; }
            }
            if (fail) break;
            lastData = lastPoll = now;
        }
    }
    void convert(const BYTE* data, UINT32 frames) {   // interleaved device format → mono float
        tmp_.resize(frames);
        const int ch = channels_; const float inv = 1.f / (float)ch;
        switch (kind_) {
        case K_F32: { const float* p = (const float*)data;
            for (UINT32 f = 0; f < frames; ++f) { float s = 0; for (int c = 0; c < ch; ++c) s += *p++; tmp_[f] = s * inv; } break; }
        case K_I16: { const int16_t* p = (const int16_t*)data; const float k = inv / 32768.f;
            for (UINT32 f = 0; f < frames; ++f) { float s = 0; for (int c = 0; c < ch; ++c) s += (float)*p++; tmp_[f] = s * k; } break; }
        case K_I32: { const int32_t* p = (const int32_t*)data; const float k = inv / 2147483648.f;
            for (UINT32 f = 0; f < frames; ++f) { float s = 0; for (int c = 0; c < ch; ++c) s += (float)*p++; tmp_[f] = s * k; } break; }
        case K_I24: { const BYTE* p = data; const float k = inv / 8388608.f;
            for (UINT32 f = 0; f < frames; ++f) { float s = 0;
                for (int c = 0; c < ch; ++c) { int32_t v = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) >> 8; s += (float)v; p += 3; }
                tmp_[f] = s * k; } break; }
        default: return;
        }
        g_ring.push(tmp_.data(), frames);
    }

    HANDLE thread_ = nullptr, stopEvent_ = nullptr;
    std::atomic<bool> deviceChanged_{false};
    std::atomic<int> sampleRate_{48000};
    Kind kind_ = K_NONE;
    int channels_ = 2;
    std::vector<float> tmp_;
    CRITICAL_SECTION cs_;
    std::wstring name_;
};
static LoopbackCapture g_capture;

// --------------------------------------------------------------------------- spectrum
class Spectrum {
public:
    static constexpr int N = cfg::FFT_N;
    static constexpr int BINS = N / 2 + 1;
    Spectrum() : win_(N), cosT_(N / 2), sinT_(N / 2), re_(N), im_(N), tilt_(BINS, 0.f) {
        for (int i = 0; i < N; ++i) win_[i] = 0.5f * (1.f - cosf(2.f * 3.14159265f * i / (N - 1)));
        for (int k = 0; k < N / 2; ++k) { cosT_[k] = cosf(2.f * 3.14159265f * k / N); sinT_[k] = sinf(2.f * 3.14159265f * k / N); }
    }
    void compute(const float* samples, float* db, int sampleRate) {   // db: BINS values, full-scale sine ≈ 0 dB, tilted
        if (sampleRate != tiltRate_) buildTilt(sampleRate);
        for (int i = 0; i < N; ++i) { re_[i] = samples[i] * win_[i]; im_[i] = 0.f; }
        fft();
        const float norm = 4.f / N;
        for (int k = 0; k < BINS; ++k) {
            float m = sqrtf(re_[k] * re_[k] + im_[k] * im_[k]) * norm;
            db[k] = std::max(20.f * log10f(m + 1e-7f), -140.f) + tilt_[k];
        }
    }
private:
    void buildTilt(int sr) {
        tiltRate_ = sr;
        const float binHz = (float)sr / N;
        for (int k = 0; k < BINS; ++k) {
            float t = cfg::TILT_DB_OCT * log2f(std::max(k * binHz, 1.f) / cfg::TILT_FROM_HZ);
            tilt_[k] = std::min(std::max(t, 0.f), cfg::TILT_MAX_DB);
        }
    }
    void fft() {   // in-place iterative radix-2
        for (int i = 1, j = 0; i < N; ++i) {
            int bit = N >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) { std::swap(re_[i], re_[j]); std::swap(im_[i], im_[j]); }
        }
        for (int len = 2; len <= N; len <<= 1) {
            const int half = len >> 1, step = N / len;
            for (int i = 0; i < N; i += len)
                for (int k = 0; k < half; ++k) {
                    const float wr = cosT_[k * step], wi = -sinT_[k * step];
                    const int a = i + k, b = a + half;
                    const float xr = re_[b] * wr - im_[b] * wi, xi = re_[b] * wi + im_[b] * wr;
                    re_[b] = re_[a] - xr; im_[b] = im_[a] - xi; re_[a] += xr; im_[a] += xi;
                }
        }
    }
    std::vector<float> win_, cosT_, sinT_, re_, im_, tilt_;
    int tiltRate_ = 0;
};

// --------------------------------------------------------------------------- DPI / dark mode helpers
typedef UINT (WINAPI* GetDpiForWindowFn)(HWND);
static GetDpiForWindowFn g_getDpiForWindow = nullptr;
static void enablePerMonitorDpi() {
    HMODULE u = GetModuleHandleW(L"user32.dll");
    if (!u) return;
    typedef BOOL (WINAPI* SetCtxFn)(HANDLE);
    auto setCtx = (SetCtxFn)GetProcAddress(u, "SetProcessDpiAwarenessContext");
    if (setCtx) setCtx((HANDLE)-4 /* PER_MONITOR_AWARE_V2 */); else SetProcessDPIAware();
    g_getDpiForWindow = (GetDpiForWindowFn)GetProcAddress(u, "GetDpiForWindow");
}
static UINT dpiOf(HWND h) {
    if (g_getDpiForWindow) { UINT d = g_getDpiForWindow(h); if (d) return d; }
    HDC dc = GetDC(nullptr); UINT d = (UINT)GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(nullptr, dc);
    return d ? d : 96;
}

// Dark popup menus use uxtheme's undocumented-but-stable ordinals (Windows 10 1903+ and
// Windows 11).  Resolved at run time; if anything is missing the menu simply stays light.
typedef int  (WINAPI* SetPreferredAppModeFn)(int);
typedef void (WINAPI* FlushMenuThemesFn)();
typedef void (WINAPI* RefreshImmersiveColorPolicyStateFn)();
static SetPreferredAppModeFn g_setPreferredAppMode = nullptr;
static FlushMenuThemesFn g_flushMenuThemes = nullptr;
static RefreshImmersiveColorPolicyStateFn g_refreshImmersiveColorPolicyState = nullptr;
static void initDarkModeApis() {
    if (g_build < 18362) return;
    HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!ux) return;
    g_setPreferredAppMode = (SetPreferredAppModeFn)GetProcAddress(ux, MAKEINTRESOURCEA(135));
    g_flushMenuThemes = (FlushMenuThemesFn)GetProcAddress(ux, MAKEINTRESOURCEA(136));
    g_refreshImmersiveColorPolicyState = (RefreshImmersiveColorPolicyStateFn)GetProcAddress(ux, MAKEINTRESOURCEA(104));
}
static void applyMenuTheme(bool light) {
    if (!g_setPreferredAppMode) return;
    g_setPreferredAppMode(light ? 3 /* ForceLight */ : 2 /* ForceDark */);
    if (g_refreshImmersiveColorPolicyState) g_refreshImmersiveColorPolicyState();
    if (g_flushMenuThemes) g_flushMenuThemes();
}

// --------------------------------------------------------------------------- overlays
static HINSTANCE g_hInst = nullptr;
static HWND g_hwndMain = nullptr;
static const wchar_t* OVERLAY_CLASS = L"WavesillOverlay";
static const wchar_t* MAIN_CLASS = L"WavesillMain";
static const wchar_t* ABOUT_CLASS = L"WavesillAbout";
static bool g_light = false;

struct Overlay {
    HWND taskbar = nullptr;
    HWND hwnd = nullptr;
    RECT rc{};                 // taskbar rect, physical screen px
    int  edge = ABE_BOTTOM;
    UINT dpi = 96;
    int  w = 0, h = 0;
    int  inset = 0;            // px kept clear at the inner edge (Windows 11 taskbar border)
    HDC memDC = nullptr; HBITMAP dib = nullptr; HGDIOBJ oldBmp = nullptr; uint32_t* px = nullptr;   // the frame (premultiplied BGRA)
    int  pitch = 4, gap = 1, nbands = 0, start = 0;
    std::vector<float> fLo, fHi, cur, tgt, pk, pkVel, pkHold, pkS;
    std::vector<float> env, penv;
    std::wstring monDev, monName;   // "\\.\DISPLAY1" / "DISPLAY1"
    bool primary = false;
    bool shown = false;        // overlay window visible
    bool needPresent = true;   // something changed since last UpdateLayeredWindow
    bool blank = true;         // nothing drawn (all levels at zero)
    bool dirty = false;        // the frame changed this tick
    bool alive = true;
    bool viaMod = false;       // the Windhawk mod is displaying this taskbar right now
    int  zTick = 0;
    int  slot = -1;            // bridge slot index, -1 = none
    bool horizontal() const { return edge == ABE_BOTTOM || edge == ABE_TOP; }
    int  length() const { return horizontal() ? w : h; }              // along the taskbar
    int  depth() const { return (horizontal() ? h : w) - inset; }     // drawable, across the taskbar
};
static std::vector<Overlay> g_overlays;

static LRESULT CALLBACK OverlayProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_NCHITTEST:     return HTTRANSPARENT;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_DPICHANGED:    return 0;
    case WM_ERASEBKGND:    return 1;
    }
    return DefWindowProcW(h, m, w, l);
}
static void overlayFreeSurface(Overlay& o) {
    if (o.memDC) { if (o.oldBmp) SelectObject(o.memDC, o.oldBmp); DeleteDC(o.memDC); }
    if (o.dib) DeleteObject(o.dib);
    o.memDC = nullptr; o.dib = nullptr; o.oldBmp = nullptr; o.px = nullptr;
}
static bool overlayAllocSurface(Overlay& o) {
    overlayFreeSurface(o);
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = o.w; bi.bmiHeader.biHeight = -o.h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    o.dib = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!o.dib || !bits) { o.dib = nullptr; return false; }
    o.memDC = CreateCompatibleDC(nullptr);
    o.oldBmp = SelectObject(o.memDC, o.dib);
    o.px = static_cast<uint32_t*>(bits);
    memset(o.px, 0, (size_t)o.w * o.h * 4);
    return true;
}
static void overlayLayout(Overlay& o) {
    const float s = o.dpi / 96.f;
    o.pitch = std::max(2, (int)lroundf(cfg::BAR_PITCH * s));
    o.gap = std::max(1, (int)lroundf(cfg::BAR_GAP * s));
    if (o.gap >= o.pitch) o.gap = o.pitch - 1;
    const int length = o.length();
    o.nbands = std::max(2, length / o.pitch);
    o.start = (length - o.nbands * o.pitch + o.gap) / 2;
    o.fLo.assign(o.nbands, 0.f); o.fHi.assign(o.nbands, 0.f);
    o.cur.assign(o.nbands, 0.f); o.tgt.assign(o.nbands, 0.f);
    o.pk.assign(o.nbands, 0.f); o.pkVel.assign(o.nbands, 0.f); o.pkHold.assign(o.nbands, 0.f); o.pkS.assign(o.nbands, 0.f);
    o.env.assign((size_t)length * cfg::CURVE_SS, 0.f); o.penv.assign((size_t)length * cfg::CURVE_SS, 0.f);
    const float ratio = cfg::FREQ_MAX / cfg::FREQ_MIN;
    for (int i = 0; i < o.nbands; ++i) {
        o.fLo[i] = cfg::FREQ_MIN * powf(ratio, (float)i / o.nbands);
        o.fHi[i] = cfg::FREQ_MIN * powf(ratio, (float)(i + 1) / o.nbands);
    }
    o.blank = true; o.needPresent = true; o.dirty = true;
}
static void overlayCreate(Overlay& o, HWND taskbar) {
    o.taskbar = taskbar;
    o.hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
                             OVERLAY_CLASS, L"Wavesill", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, g_hInst, nullptr);
}
static void overlayDestroy(Overlay& o) {
    overlayFreeSurface(o);
    if (o.hwnd) DestroyWindow(o.hwnd);
    o.hwnd = nullptr;
}
static int inferEdge(const RECT& r, const RECT& m) {
    const int w = r.right - r.left, h = r.bottom - r.top;
    if (w >= h) return (std::abs(r.top - m.top) <= std::abs(m.bottom - r.bottom)) ? ABE_TOP : ABE_BOTTOM;
    return (std::abs(r.left - m.left) <= std::abs(m.right - r.right)) ? ABE_LEFT : ABE_RIGHT;
}
static const Visual& overlayVisual(const Overlay& o) {
    const MonitorPrefs& p = monitorPrefs(o.monDev);
    return p.override_ ? p.v : g_settings.global;
}
static int borderInsetFor(UINT dpi) {
    if (g_settings.borderInset >= 0) return (int)lroundf(g_settings.borderInset * dpi / 96.f);
    return g_win11 ? std::max(1, (int)lroundf(dpi / 96.f)) : 0;   // Windows 11 draws a 1-DIP top border
}
// Reads the taskbar's current rect/DPI/edge; (re)builds the surface when anything changed.
static bool overlayUpdateGeometry(Overlay& o) {
    if (!IsWindow(o.taskbar)) return false;
    RECT r{}; if (!GetWindowRect(o.taskbar, &r)) return false;
    HMONITOR mon = MonitorFromWindow(o.taskbar, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW mi{}; mi.cbSize = sizeof mi; GetMonitorInfoW(mon, &mi);
    const int edge = inferEdge(r, mi.rcMonitor);
    const UINT dpi = dpiOf(o.taskbar);
    const int w = r.right - r.left, h = r.bottom - r.top;
    std::wstring dev = mi.szDevice[0] ? mi.szDevice : L"\\\\.\\DISPLAY?";
    if (dev != o.monDev) {
        o.monDev = dev;
        size_t p = dev.find_last_of(L'\\');
        o.monName = (p != std::wstring::npos) ? dev.substr(p + 1) : dev;
    }
    o.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384) return true;
    const int inset = borderInsetFor(dpi);
    const bool sizeChanged = (w != o.w || h != o.h || edge != o.edge || dpi != o.dpi || inset != o.inset || !o.px);
    const bool moved = (r.left != o.rc.left || r.top != o.rc.top);
    o.rc = r; o.edge = edge; o.dpi = dpi; o.inset = inset;
    if (sizeChanged) {
        o.w = w; o.h = h;
        if (!overlayAllocSurface(o)) { o.w = o.h = 0; return true; }
        overlayLayout(o);
    } else if (moved) o.needPresent = true;
    return true;
}
static bool isShellLike(HWND h) {
    if (!h || h == GetDesktopWindow() || h == GetShellWindow()) return true;
    wchar_t cls[64] = L""; GetClassNameW(h, cls, 64);
    return !wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd") ||
           !wcscmp(cls, L"Shell_SecondaryTrayWnd") || !wcscmp(cls, OVERLAY_CLASS);
}
// Hidden taskbar, auto-hide slid away, or a full-screen window owning that monitor.
static bool overlayShouldHide(const Overlay& o) {
    if (!IsWindowVisible(o.taskbar)) return true;
    HMONITOR mon = MonitorFromWindow(o.taskbar, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{}; mi.cbSize = sizeof mi; GetMonitorInfoW(mon, &mi);
    RECT vis{};
    if (!IntersectRect(&vis, &o.rc, &mi.rcMonitor)) return true;
    const long visArea = (vis.right - vis.left) * (vis.bottom - vis.top), fullArea = (long)o.w * o.h;
    if (fullArea > 0 && visArea * 4 < fullArea) return true;
    HWND fg = GetForegroundWindow();
    if (fg && fg != o.taskbar && !isShellLike(fg) && !IsZoomed(fg) && !IsIconic(fg) &&
        MonitorFromWindow(fg, MONITOR_DEFAULTTONULL) == mon) {
        RECT r{}; GetWindowRect(fg, &r);
        if (r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top && r.right >= mi.rcMonitor.right && r.bottom >= mi.rcMonitor.bottom) return true;
    }
    return false;
}
static void overlayEnsureTop(Overlay& o) {
    for (HWND h = GetTopWindow(nullptr); h; h = GetWindow(h, GW_HWNDNEXT)) {
        if (h == o.hwnd) return;
        if (h == o.taskbar) break;
    }
    SetWindowPos(o.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
}

// ---- pixel helpers in "logical" coordinates: x along the taskbar (0..length), y from the
//      screen edge (0 = root) into the screen (0..depth).  The edge mapping lives here, so
//      every style is written once and works on all four edges.
static inline uint32_t pmColor(int alpha, bool black) {   // premultiplied BGRA
    alpha = std::min(std::max(alpha, 0), 255);
    return black ? ((uint32_t)alpha << 24) : ((uint32_t)alpha << 24 | (uint32_t)alpha << 16 | (uint32_t)alpha << 8 | (uint32_t)alpha);
}
static inline uint32_t pmScale(uint32_t c, float f) {
    if (f >= 1.f) return c;
    if (f <= 0.f) return 0;
    uint32_t a = (uint32_t)((c >> 24) * f + 0.5f), r = (uint32_t)(((c >> 16) & 255) * f + 0.5f);
    uint32_t g = (uint32_t)(((c >> 8) & 255) * f + 0.5f), b = (uint32_t)((c & 255) * f + 0.5f);
    return a << 24 | r << 16 | g << 8 | b;
}
static inline uint32_t* pixelAt(Overlay& o, int x, int y) {
    if (y >= o.depth() || y < 0) return nullptr;
    int px, py;
    switch (o.edge) {
    case ABE_BOTTOM: px = x; py = o.h - 1 - y; break;
    case ABE_TOP:    px = x; py = y; break;
    case ABE_LEFT:   px = y; py = x; break;
    default:         px = o.w - 1 - y; py = x; break;
    }
    if (px < 0 || py < 0 || px >= o.w || py >= o.h) return nullptr;
    return o.px + (size_t)py * o.w + px;
}
static inline void blendPx(Overlay& o, int x, int y, uint32_t src) {   // premultiplied "over"
    uint32_t* d = pixelAt(o, x, y);
    if (!d || !src) return;
    uint32_t dst = *d;
    if (!dst) { *d = src; return; }
    const float k = 1.f - (src >> 24) / 255.f;
    uint32_t a = std::min<uint32_t>(255, (src >> 24) + (uint32_t)((dst >> 24) * k + 0.5f));
    uint32_t r = std::min<uint32_t>(255, ((src >> 16) & 255) + (uint32_t)(((dst >> 16) & 255) * k + 0.5f));
    uint32_t g = std::min<uint32_t>(255, ((src >> 8) & 255) + (uint32_t)(((dst >> 8) & 255) * k + 0.5f));
    uint32_t b = std::min<uint32_t>(255, (src & 255) + (uint32_t)((dst & 255) * k + 0.5f));
    *d = a << 24 | r << 16 | g << 8 | b;
}
static inline void fillCol(Overlay& o, int x, int y0, int y1, uint32_t c) {   // [y0, y1)
    y0 = std::max(y0, 0); y1 = std::min(y1, o.depth());
    for (int y = y0; y < y1; ++y) { uint32_t* d = pixelAt(o, x, y); if (d) *d = c; }
}
// Column [0, top) with opacity rising from FADE_FLOOR at the root to 1 at `top` (bars).
static inline void fillColFade(Overlay& o, int x, float top, uint32_t c) {
    const int y1 = std::min((int)top, o.depth());
    for (int y = 0; y < y1; ++y) {
        const float f = cfg::FADE_FLOOR + (1.f - cfg::FADE_FLOOR) * ((y + 0.5f) / top);
        uint32_t* d = pixelAt(o, x, y); if (d) *d = pmScale(c, f);
    }
}
// ---- curve styles.  The envelope is sampled CURVE_SS times per pixel column (at the sub-pixel
//      centres), and every edge pixel gets the exact vertical coverage averaged over those
//      samples, so slopes of any steepness come out smooth.
// Catmull-Rom through the band values → px per sub-sample; out has length() * CURVE_SS entries.
static void buildEnvelope(const Overlay& o, const float* v, float* out) {
    const int n = o.nbands, total = o.length() * cfg::CURVE_SS;
    const float depth = (float)o.depth();
    const float c0 = o.start + (o.pitch - o.gap) * 0.5f;
    for (int j = 0; j < total; ++j) {
        const float u = ((j + 0.5f) / cfg::CURVE_SS - c0) / o.pitch;
        int i = (int)floorf(u); float t = u - i;
        if (i < 0) { i = 0; t = 0.f; }
        if (i >= n - 1) { i = n - 1; t = 0.f; }
        const float p0 = v[std::max(i - 1, 0)], p1 = v[i], p2 = v[std::min(i + 1, n - 1)], p3 = v[std::min(i + 2, n - 1)];
        const float y = 0.5f * ((2.f * p1) + (-p0 + p2) * t + (2.f * p0 - 5.f * p1 + 4.f * p2 - p3) * t * t + (-p0 + 3.f * p1 - 3.f * p2 + p3) * t * t * t);
        out[j] = std::min(std::max(y, 0.f), 1.f) * depth;
    }
}
// The area under the envelope; with fade, opacity rises from FADE_FLOOR at the root to 1 at the top.
static void fillUnder(Overlay& o, const float* env, int length, uint32_t c, bool fade) {
    constexpr int SS = cfg::CURVE_SS;
    for (int x = 0; x < length; ++x) {
        const float* e = env + x * SS;
        float lo = e[0], hi = e[0], mean = 0.f;
        for (int k = 0; k < SS; ++k) { lo = std::min(lo, e[k]); hi = std::max(hi, e[k]); mean += e[k]; }
        mean /= SS;
        if (hi <= 0.f) continue;
        const int yFull = (int)floorf(lo), yEnd = std::min((int)ceilf(hi), o.depth());
        for (int y = 0; y < yEnd; ++y) {
            float cov = 1.f;
            if (y >= yFull) { cov = 0.f; for (int k = 0; k < SS; ++k) cov += std::min(std::max(e[k] - y, 0.f), 1.f); cov /= SS; }
            if (fade) cov *= std::min(1.f, cfg::FADE_FLOOR + (1.f - cfg::FADE_FLOOR) * ((y + 0.5f) / mean));
            if (cov > 0.f) { uint32_t* d = pixelAt(o, x, y); if (d) *d = pmScale(c, cov); }
        }
    }
}
// A stroke of width wpx along the envelope.  Per sub-sample the stroke's vertical extent is
// wpx * sqrt(1 + slope²).  fadeIn: the stroke fades out within this many px of the root;
// onlyAbove: only drawn where it rises at least 2 px above that envelope (the peak-hold line).
static void strokeCurve(Overlay& o, const float* env, int length, float wpx, uint32_t c, const float* onlyAbove = nullptr, float fadeIn = 3.f) {
    constexpr int SS = cfg::CURVE_SS;
    const int total = length * SS;
    for (int x = 0; x < length; ++x) {
        const float* e = env + x * SS;
        float mean = 0.f; for (int k = 0; k < SS; ++k) mean += e[k]; mean /= SS;
        float f = std::min(1.f, mean / fadeIn);
        if (onlyAbove) {
            float m2 = 0.f; for (int k = 0; k < SS; ++k) m2 += onlyAbove[x * SS + k]; m2 /= SS;
            f *= std::min(1.f, std::max(0.f, (mean - m2) / 2.f));
        }
        if (f <= 0.f) continue;
        float a[SS], b[SS], lo = 1e9f, hi = -1e9f;
        for (int k = 0; k < SS; ++k) {
            const int j = x * SS + k;
            const float yp = env[j > 0 ? j - 1 : j], yn = env[j + 1 < total ? j + 1 : j];
            const float slope = (yn - yp) * (0.5f * SS), thick = wpx * sqrtf(1.f + slope * slope);
            a[k] = e[k] - thick * 0.5f; b[k] = e[k] + thick * 0.5f;
            lo = std::min(lo, a[k]); hi = std::max(hi, b[k]);
        }
        const int yEnd = std::min(o.depth(), (int)ceilf(hi));
        for (int y = std::max(0, (int)floorf(lo)); y < yEnd; ++y) {
            float cov = 0.f;
            for (int k = 0; k < SS; ++k) cov += std::max(0.f, std::min((float)y + 1, b[k]) - std::max((float)y, a[k]));
            cov /= SS;
            if (cov > 0.f) blendPx(o, x, y, pmScale(c, cov * f));
        }
    }
}
static float bandDb(const float* db, int bins, float binHz, float fLo, float fHi) {
    float bLo = fLo / binHz, bHi = fHi / binHz;
    if (bHi - bLo < 1.f) {
        float bc = 0.5f * (bLo + bHi);
        int k = (int)bc; if (k >= bins - 1) k = bins - 2; if (k < 0) k = 0;
        float t = std::min(std::max(bc - k, 0.f), 1.f);
        return db[k] + (db[k + 1] - db[k]) * t;
    }
    int k0 = std::max(0, (int)bLo), k1 = std::min(bins - 1, (int)ceilf(bHi));
    float m = -200.f;
    for (int k = k0; k <= k1; ++k) m = std::max(m, db[k]);
    return m;
}
static void smooth3(std::vector<float>& v, int passes) {
    const int n = (int)v.size();
    for (int pass = 0; pass < passes; ++pass) {
        float prev = v[0];
        for (int i = 0; i < n; ++i) {
            const float l = prev, m = v[i], r = v[i + 1 < n ? i + 1 : i];
            prev = m; v[i] = 0.25f * l + 0.5f * m + 0.25f * r;
        }
    }
}

static void overlayFrame(Overlay& o, const float* db, int bins, float binHz, float floorDb, float dt) {
    o.dirty = false;
    if (!o.px || o.nbands <= 0) return;
    const Visual& vis = overlayVisual(o);
    const bool black = vis.color == COL_BLACK || (vis.color == COL_THEME && g_light);
    const int alpha = cfg::OPACITY_PCT[vis.opacity] * 255 / 100;
    const int n = o.nbands;
    const bool curve = vis.style != ST_BARS;
    const bool hold = vis.fall == FALL_PEAK_HOLD;

    for (int i = 0; i < n; ++i) {
        float v = bandDb(db, bins, binHz, o.fLo[i], o.fHi[i]);
        o.tgt[i] = (v <= cfg::GATE_DB) ? 0.f : std::min(std::max((v - floorDb) / cfg::RANGE_DB, 0.f), 1.f);
    }
    smooth3(o.tgt, curve ? 2 : 1);
    const float ka = 1.f - expf(-dt / cfg::ATTACK_S), kr = 1.f - expf(-dt / cfg::RELEASE_S);
    bool any = false;
    for (int i = 0; i < n; ++i) {
        const float t = o.tgt[i];
        float c = o.cur[i];
        c += (t - c) * (t > c ? ka : kr);
        if (c < 0.003f) c = 0.f;
        o.cur[i] = c;
        if (c > 0.f) any = true;
        if (hold) {
            float p = o.pk[i];
            if (c >= p) { p = c; o.pkVel[i] = 0.f; o.pkHold[i] = cfg::PEAK_HOLD_S; }
            else {
                o.pkHold[i] -= dt;
                if (o.pkHold[i] <= 0.f) { o.pkVel[i] += cfg::PEAK_GRAVITY * dt; p -= o.pkVel[i] * dt; }
                if (p < c) { p = c; o.pkVel[i] = 0.f; }
            }
            if (p < 0.003f) p = 0.f;
            o.pk[i] = p;
            if (p > 0.f) any = true;
        } else { o.pk[i] = c; o.pkVel[i] = 0.f; o.pkHold[i] = 0.f; }
    }
    if (!any && o.blank) return;   // settled at silence: nothing to draw

    memset(o.px, 0, (size_t)o.w * o.h * 4);
    const float s = o.dpi / 96.f;
    const int depth = o.depth();
    const uint32_t col = pmColor(alpha, black);
    const uint32_t colEdge = pmColor(alpha * 3 / 2, black);
    const uint32_t colLine = pmColor(alpha * 2, black);
    const uint32_t colPeak = pmColor(alpha * 3 / 2, black);

    if (vis.style == ST_BARS) {
        const int bw = o.pitch - o.gap;
        const int capH = std::max(1, (int)lroundf(cfg::CAP_H * s));
        const bool fade = vis.fade;
        for (int i = 0; i < n; ++i) {
            const int x0 = o.start + i * o.pitch;
            const int len = std::min(depth, (int)lroundf(o.cur[i] * depth));
            if (len > 0) {
                for (int x = x0; x < x0 + bw; ++x) {
                    if (fade) fillColFade(o, x, (float)len, col); else fillCol(o, x, 0, len - 1, col);
                    fillCol(o, x, len - 1, len, colEdge);
                }
            }
            if (hold) {
                const int pl = std::min(depth, (int)lroundf(o.pk[i] * depth));
                if (pl - len > capH) for (int x = x0; x < x0 + bw; ++x) fillCol(o, x, pl - capH, pl, colPeak);
            }
        }
    } else {
        const int length = o.length();
        buildEnvelope(o, o.cur.data(), o.env.data());
        if (hold) { o.pkS = o.pk; smooth3(o.pkS, 3); buildEnvelope(o, o.pkS.data(), o.penv.data()); }
        const float lineW = cfg::LINE_W * s, peakW = cfg::PEAK_LINE_W * s;
        if (vis.style == ST_FILLED) fillUnder(o, o.env.data(), length, col, vis.fade);
        else {   // ST_LINE: optional fade below, then the curve itself
            if (vis.fade) fillUnder(o, o.env.data(), length, col, true);
            strokeCurve(o, o.env.data(), length, lineW, colLine);
        }
        if (hold) strokeCurve(o, o.penv.data(), length, peakW, colPeak, o.env.data());
    }
    o.blank = !any;
    o.dirty = true;
    o.needPresent = true;
}

static void overlayPresent(Overlay& o) {
    if (!o.px) return;
    POINT src{0, 0}, dst{o.rc.left, o.rc.top};
    SIZE sz{o.w, o.h};
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(o.hwnd, nullptr, &dst, &sz, o.memDC, &src, 0, &bf, ULW_ALPHA);
    o.needPresent = false;
    if (!o.shown) { ShowWindow(o.hwnd, SW_SHOWNOACTIVATE); o.shown = true; overlayEnsureTop(o); }
}
static void overlayHide(Overlay& o) {
    if (o.shown) { ShowWindow(o.hwnd, SW_HIDE); o.shown = false; }
    o.needPresent = true;
}
static void redrawAll() { for (auto& o : g_overlays) { o.needPresent = true; o.blank = false; } }

static void refreshTaskbars() {
    std::vector<HWND> found;
    if (HWND p = FindWindowW(L"Shell_TrayWnd", nullptr)) found.push_back(p);
    for (HWND s = nullptr; (s = FindWindowExW(nullptr, s, L"Shell_SecondaryTrayWnd", nullptr)) != nullptr;) found.push_back(s);
    for (auto& o : g_overlays) o.alive = IsWindow(o.taskbar) && std::find(found.begin(), found.end(), o.taskbar) != found.end();
    for (auto it = g_overlays.begin(); it != g_overlays.end();) {
        if (!it->alive) { overlayDestroy(*it); it = g_overlays.erase(it); } else ++it;
    }
    for (HWND tb : found) {
        bool have = false;
        for (auto& o : g_overlays) if (o.taskbar == tb) { have = true; break; }
        if (!have) { g_overlays.emplace_back(); overlayCreate(g_overlays.back(), tb); }
    }
}
static const wchar_t* edgeName(int edge) {
    switch (edge) { case ABE_TOP: return S(S_EDGE_TOP); case ABE_LEFT: return S(S_EDGE_LEFT); case ABE_RIGHT: return S(S_EDGE_RIGHT); default: return S(S_EDGE_BOTTOM); }
}
static std::wstring overlayLabel(const Overlay& o) {
    wchar_t buf[160];
    _snwprintf_s(buf, 160, _TRUNCATE, L"%s%s%s   %d\u00D7%d   %s", o.monName.c_str(), o.primary ? L" \u00B7 " : L"",
                 o.primary ? S(S_PRIMARY) : L"", o.w, o.h, edgeName(o.edge));
    return buf;
}

// --------------------------------------------------------------------------- bridge client (optional Windhawk mod)
enum BridgeStatus { BR_NONE = 0, BR_CONNECTED, BR_INCOMPATIBLE, BR_STALE };
struct Bridge {
    HANDLE map = nullptr; uint8_t* base = nullptr; size_t size = 0;
    WsBridgeHeader* hdr = nullptr;
    BridgeStatus status = BR_NONE;
    uint32_t seenProtocol = 0, modVersion = 0;
    ULONGLONG lastTry = 0;
    uint32_t stageFlags() const { return (status == BR_CONNECTED && hdr) ? hdr->flags : 0; }
};
static Bridge g_bridge;

static WsBridgeSlot* bridgeSlot(int i) { return (WsBridgeSlot*)(g_bridge.base + g_bridge.hdr->headerSize + (size_t)i * g_bridge.hdr->slotSize); }
static void bridgeClose() {
    if (g_bridge.base && g_bridge.hdr && g_bridge.status == BR_CONNECTED) {
        for (auto& o : g_overlays) if (o.slot >= 0) { WsBridgeSlot* s = bridgeSlot(o.slot); s->visible = 0; MemoryBarrier(); s->taskbarHwnd = 0; }
        g_bridge.hdr->appHeartbeatMs = 0;
    }
    for (auto& o : g_overlays) { o.slot = -1; o.viaMod = false; }
    if (g_bridge.base) UnmapViewOfFile(g_bridge.base);
    if (g_bridge.map) CloseHandle(g_bridge.map);
    g_bridge.map = nullptr; g_bridge.base = nullptr; g_bridge.hdr = nullptr; g_bridge.size = 0;
    if (g_bridge.status == BR_CONNECTED) g_bridge.status = BR_NONE;
}
static void bridgeTryOpen() {
    HANDLE m = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, WS_BRIDGE_MAPPING_NAME);
    if (!m) { g_bridge.status = BR_NONE; return; }
    uint8_t* base = (uint8_t*)MapViewOfFile(m, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
    if (!base) { CloseHandle(m); g_bridge.status = BR_NONE; return; }
    MEMORY_BASIC_INFORMATION mbi{}; VirtualQuery(base, &mbi, sizeof mbi);
    const size_t size = mbi.RegionSize;
    auto* h = (WsBridgeHeader*)base;
    bool ok = size >= sizeof(WsBridgeHeader) && h->magic == WS_BRIDGE_MAGIC;
    if (ok && GetTickCount64() - h->modHeartbeatMs > 3000) {   // mapping outlived its mod
        UnmapViewOfFile(base); CloseHandle(m); g_bridge.status = BR_STALE; return;
    }
    if (ok && h->protocol != WS_BRIDGE_PROTOCOL) { g_bridge.status = BR_INCOMPATIBLE; g_bridge.seenProtocol = h->protocol; g_bridge.modVersion = h->modVersion; ok = false; }
    if (ok) {
        ok = h->headerSize >= sizeof(WsBridgeHeader) && h->slotSize >= sizeof(WsBridgeSlot) && h->slotCount >= 1 && h->slotCount <= WS_BRIDGE_MAX_SLOTS &&
             h->totalBytes <= size && (size_t)h->headerSize + (size_t)h->slotCount * h->slotSize <= h->totalBytes;
        for (uint32_t i = 0; ok && i < h->slotCount; ++i) {
            auto* s = (WsBridgeSlot*)(base + h->headerSize + (size_t)i * h->slotSize);
            for (int b = 0; b < 2; ++b) ok = ok && s->bufferBytes >= WS_BRIDGE_MIN_SLOT_BYTES / 2 && (size_t)s->bufferOffset[b] + s->bufferBytes <= h->totalBytes;
        }
        if (!ok) { g_bridge.status = BR_INCOMPATIBLE; g_bridge.seenProtocol = h->protocol; }
    }
    if (!ok) { UnmapViewOfFile(base); CloseHandle(m); return; }
    g_bridge.map = m; g_bridge.base = base; g_bridge.size = size; g_bridge.hdr = h;
    g_bridge.modVersion = h->modVersion; g_bridge.seenProtocol = h->protocol;
    g_bridge.status = BR_CONNECTED;
    h->appProtocol = WS_BRIDGE_PROTOCOL; h->appVersion = WS_VERSION_U32;
}
static void bridgeMaintain(ULONGLONG now) {   // once per taskbar scan
    if (g_bridge.status == BR_CONNECTED) {
        if (now - g_bridge.hdr->modHeartbeatMs > 3000) { bridgeClose(); g_bridge.status = BR_STALE; }
        return;
    }
    if (now - g_bridge.lastTry < 1000) return;
    g_bridge.lastTry = now;
    bridgeTryOpen();
}
// Hand this overlay's frame to the mod.  Returns true when the mod is displaying it.
static bool bridgePush(Overlay& o, bool visible, ULONGLONG now) {
    if (g_bridge.status != BR_CONNECTED || !o.px) { o.slot = -1; return false; }
    WsBridgeHeader* h = g_bridge.hdr;
    const uint64_t hwnd = (uint64_t)(uintptr_t)o.taskbar;
    if (o.slot >= 0 && bridgeSlot(o.slot)->taskbarHwnd != hwnd) o.slot = -1;
    if (o.slot < 0) {
        for (uint32_t i = 0; i < h->slotCount && o.slot < 0; ++i) if (bridgeSlot(i)->taskbarHwnd == hwnd) o.slot = (int)i;
        for (uint32_t i = 0; i < h->slotCount && o.slot < 0; ++i) {
            WsBridgeSlot* s = bridgeSlot(i);
            if (s->taskbarHwnd == 0 || now - s->appHeartbeatMs > 5000) { s->taskbarHwnd = hwnd; s->front = 0; s->frameSeq = 0; o.slot = (int)i; o.dirty = true; }
        }
        if (o.slot < 0) return false;
    }
    WsBridgeSlot* s = bridgeSlot(o.slot);
    const uint32_t bytes = (uint32_t)o.w * 4 * (uint32_t)o.h;
    if (bytes > s->bufferBytes) { s->visible = 0; s->appHeartbeatMs = now; return false; }   // too big for the mod's buffer
    if (o.dirty || s->width != o.w || s->height != o.h) {
        const uint32_t back = s->front ^ 1;
        memcpy(g_bridge.base + s->bufferOffset[back], o.px, bytes);
        s->x = o.rc.left; s->y = o.rc.top; s->width = o.w; s->height = o.h; s->stride = (uint32_t)o.w * 4; s->dpi = o.dpi; s->edge = (uint32_t)o.edge;
        s->flags = o.primary ? WS_SLOT_PRIMARY : 0;
        MemoryBarrier();
        s->front = back; s->frameSeq++;
    } else { s->x = o.rc.left; s->y = o.rc.top; s->flags = o.primary ? WS_SLOT_PRIMARY : 0; }
    s->visible = visible ? 1u : 0u;
    MemoryBarrier();
    s->appHeartbeatMs = now;
    h->appHeartbeatMs = now;
    return now - s->modHeartbeatMs < WS_BRIDGE_FRESH_MS && s->modStatus == 1;
}

// --------------------------------------------------------------------------- tray icon
enum { WM_TRAY = WM_APP + 1 };
static NOTIFYICONDATAW g_nid{};
static UINT g_msgTaskbarCreated = 0;
static bool g_customTrayAvailable = false;
static std::wstring g_trayIconSource;

static std::wstring customTrayPath() {
    const std::wstring a = g_trayDir + (g_light ? L"\\tray_light.ico" : L"\\tray_dark.ico"), b = g_trayDir + L"\\tray.ico";
    if (fileExists(a)) return a;
    if (fileExists(b)) return b;
    return L"";
}
static void refreshCustomTrayAvailability() {
    g_customTrayAvailable = fileExists(g_trayDir + L"\\tray_dark.ico") || fileExists(g_trayDir + L"\\tray_light.ico") || fileExists(g_trayDir + L"\\tray.ico");
}
static HICON trayIcon() {
    const int cx = GetSystemMetrics(SM_CXSMICON), cy = GetSystemMetrics(SM_CYSMICON);
    g_trayIconSource.clear();
    if (g_settings.customTray) {
        std::wstring p = customTrayPath();
        if (!p.empty()) {
            HICON h = (HICON)LoadImageW(nullptr, p.c_str(), IMAGE_ICON, cx, cy, LR_LOADFROMFILE | LR_DEFAULTCOLOR);
            if (h) { g_trayIconSource = p; return h; }
        }
    }
    HICON h = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(g_light ? IDI_TRAY_BLACK : IDI_TRAY_WHITE), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
    if (!h) h = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
    return h ? h : LoadIconW(nullptr, IDI_APPLICATION);
}
static void trayTip() {
    _snwprintf_s(g_nid.szTip, sizeof g_nid.szTip / sizeof g_nid.szTip[0], _TRUNCATE, g_zh ? L"栏声 Wavesill v%s" : L"Wavesill v%s", WS_VERSION_W);
}
static void trayAdd() {
    if (g_nid.hIcon) DestroyIcon(g_nid.hIcon);
    g_nid = {};
    g_nid.cbSize = sizeof g_nid; g_nid.hWnd = g_hwndMain; g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = trayIcon();
    trayTip();
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
}
static void trayUpdateIcon() {
    if (!g_nid.cbSize) return;
    HICON old = g_nid.hIcon;
    g_nid.hIcon = trayIcon();
    g_nid.uFlags = NIF_ICON;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    if (old) DestroyIcon(old);
}
static void trayRemove() { if (g_nid.cbSize) Shell_NotifyIconW(NIM_DELETE, &g_nid); }

// --------------------------------------------------------------------------- About window
// Hand-made so it can follow the system theme.  Text and the OK button are rendered with
// Direct2D + DirectWrite into a 32-bit DIB and copied to the window: over the Mica backdrop
// (Windows 11 22H2+) DirectWrite's grayscale anti-aliasing with proper gamma is exactly
// what XAML apps use; on a solid page it renders ClearType.  If Direct2D is unavailable
// the window falls back to a solid page with ordinary GDI ClearType text.
struct AboutRow { std::wstring label, value; bool title = false; bool indent = false; bool gapBefore = false; RECT rl{}, rv{}; };
static HWND g_hwndAbout = nullptr;
static std::vector<AboutRow> g_aboutRows;
static HFONT g_aboutFont = nullptr, g_aboutTitleFont = nullptr;       // GDI fallback
static bool g_aboutMica = false, g_aboutHover = false, g_aboutTracking = false;
static RECT g_aboutBtn{};

// ---- Direct2D / DirectWrite, resolved at run time
static const IID WS_IID_ID2D1Factory  = {0x06152247, 0x6F50, 0x465A, {0x92, 0x45, 0x11, 0x8B, 0xFD, 0x3B, 0x60, 0x07}};
static const IID WS_IID_IDWriteFactory = {0xB859EE5A, 0xD838, 0x4B5B, {0xA2, 0xE8, 0x1A, 0xDC, 0x7D, 0x93, 0xDB, 0x48}};
static ID2D1Factory* g_d2d = nullptr;
static IDWriteFactory* g_dw = nullptr;
static IDWriteTextFormat* g_dwText = nullptr;
static IDWriteTextFormat* g_dwLabel = nullptr;
static IDWriteTextFormat* g_dwTitle = nullptr;
static bool g_d2dTried = false;

static bool d2dInit() {
    if (g_d2dTried) return g_d2d && g_dw;
    g_d2dTried = true;
    typedef HRESULT (WINAPI* D2DFn)(D2D1_FACTORY_TYPE, REFIID, const D2D1_FACTORY_OPTIONS*, void**);
    typedef HRESULT (WINAPI* DWFn)(DWRITE_FACTORY_TYPE, REFIID, IUnknown**);
    HMODULE d2 = LoadLibraryExW(L"d2d1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE dw = LoadLibraryExW(L"dwrite.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto d2f = d2 ? (D2DFn)GetProcAddress(d2, "D2D1CreateFactory") : nullptr;
    auto dwf = dw ? (DWFn)GetProcAddress(dw, "DWriteCreateFactory") : nullptr;
    if (!d2f || !dwf) return false;
    if (FAILED(d2f(D2D1_FACTORY_TYPE_SINGLE_THREADED, WS_IID_ID2D1Factory, nullptr, (void**)&g_d2d))) g_d2d = nullptr;
    if (FAILED(dwf(DWRITE_FACTORY_TYPE_SHARED, WS_IID_IDWriteFactory, (IUnknown**)&g_dw))) g_dw = nullptr;
    return g_d2d && g_dw;
}
static void dwFonts(UINT dpi) {
    if (g_dwText) g_dwText->Release(); if (g_dwLabel) g_dwLabel->Release(); if (g_dwTitle) g_dwTitle->Release();
    g_dwText = g_dwLabel = g_dwTitle = nullptr;
    if (!g_dw) return;
    const wchar_t* face = g_zh ? L"Microsoft YaHei UI" : L"Segoe UI";
    const wchar_t* loc = g_zh ? L"zh-CN" : L"en-US";
    const float px = 9.f * dpi / 72.f, titlePx = 13.f * dpi / 72.f;
    g_dw->CreateTextFormat(face, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, px, loc, &g_dwText);
    g_dw->CreateTextFormat(face, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, px, loc, &g_dwLabel);
    g_dw->CreateTextFormat(face, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, titlePx, loc, &g_dwTitle);
    if (g_dwLabel) g_dwLabel->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
}
// Measures `text` in `fmt` within maxWidth px; returns the size in px.
static SIZE dwMeasure(IDWriteTextFormat* fmt, const std::wstring& text, int maxWidth) {
    SIZE sz{0, 0};
    IDWriteTextLayout* lay = nullptr;
    if (!g_dw || !fmt || FAILED(g_dw->CreateTextLayout(text.c_str(), (UINT32)text.size(), fmt, (float)std::max(maxWidth, 1), 4096.f, &lay)) || !lay) return sz;
    DWRITE_TEXT_METRICS m{}; lay->GetMetrics(&m);
    sz.cx = (LONG)ceilf(m.widthIncludingTrailingWhitespace); sz.cy = (LONG)ceilf(m.height);
    lay->Release();
    return sz;
}
static SIZE gdiMeasure(HDC dc, HFONT font, const std::wstring& text, int maxWidth, bool singleLine) {
    HGDIOBJ old = SelectObject(dc, font);
    RECT m{0, 0, maxWidth, 0};
    DrawTextW(dc, text.c_str(), -1, &m, DT_CALCRECT | DT_NOPREFIX | (singleLine ? DT_SINGLELINE : DT_WORDBREAK));
    SelectObject(dc, old);
    return SIZE{m.right - m.left, m.bottom - m.top};
}

static void aboutApplyTheme(HWND h) {
    BOOL dark = !g_light;
    if (FAILED(DwmSetWindowAttribute(h, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark)))
        DwmSetWindowAttribute(h, 19, &dark, sizeof dark);
    InvalidateRect(h, nullptr, TRUE);
}
static void aboutLayout(HWND h) {
    const UINT dpi = dpiOf(h);
    const float s = dpi / 96.f;
    const bool dw = g_d2d && g_dw;
    const wchar_t* face = g_zh ? L"Microsoft YaHei UI" : L"Segoe UI";
    if (g_aboutFont) DeleteObject(g_aboutFont);
    if (g_aboutTitleFont) DeleteObject(g_aboutTitleFont);
    g_aboutFont = CreateFontW(-(int)lroundf(9.f * dpi / 72.f), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
    g_aboutTitleFont = CreateFontW(-(int)lroundf(13.f * dpi / 72.f), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
    if (dw) dwFonts(dpi);

    const int pad = (int)(22 * s), gap = (int)(14 * s), rowGap = (int)(4 * s), sectionGap = (int)(12 * s), indent = (int)(18 * s);
    const int contentW = (int)(500 * s);
    HDC dc = GetDC(h);
    auto measure = [&](const std::wstring& t, bool title, bool label, int maxW) -> SIZE {
        if (dw) return dwMeasure(title ? g_dwTitle : label ? g_dwLabel : g_dwText, t, maxW);
        return gdiMeasure(dc, title ? g_aboutTitleFont : g_aboutFont, t, maxW, label);
    };
    int labelW = 0;
    for (auto& r : g_aboutRows) if (!r.label.empty() && !r.title) labelW = std::max(labelW, (int)measure(r.label, false, true, contentW).cx);
    int y = pad;
    for (auto& r : g_aboutRows) {
        if (r.gapBefore) y += sectionGap;
        const int x0 = pad + (r.indent ? indent : 0);
        if (r.label.empty() || r.title) {          // full-width paragraph
            SIZE m = measure(r.title ? r.label : r.value, r.title, false, pad + contentW - x0);
            r.rl = r.rv = {x0, y, pad + contentW, y + (int)m.cy};
            y = r.rv.bottom + rowGap;
        } else {
            SIZE ml = measure(r.label, false, true, contentW);
            const int vx = pad + labelW + gap;
            SIZE mv = r.value.empty() ? SIZE{0, 0} : measure(r.value, false, false, pad + contentW - vx);
            const int hh = std::max((int)ml.cy, (int)mv.cy);
            r.rl = {x0, y, x0 + labelW, y + hh};
            r.rv = {vx, y, pad + contentW, y + hh};
            y += hh + rowGap;
        }
    }
    ReleaseDC(h, dc);
    const int btnW = (int)(90 * s), btnH = (int)(32 * s);
    const int cw = contentW + 2 * pad, ch = y - rowGap + pad + btnH + pad;
    g_aboutBtn = {cw - pad - btnW, ch - pad - btnH, cw - pad, ch - pad};
    RECT wr{0, 0, cw, ch};
    AdjustWindowRectEx(&wr, (DWORD)GetWindowLongPtrW(h, GWL_STYLE), FALSE, (DWORD)GetWindowLongPtrW(h, GWL_EXSTYLE));
    POINT pt; GetCursorPos(&pt);
    MONITORINFO mi{}; mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    const int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
    SetWindowPos(h, nullptr, (mi.rcWork.left + mi.rcWork.right - ww) / 2, (mi.rcWork.top + mi.rcWork.bottom - wh) / 2, ww, wh, SWP_NOZORDER | SWP_NOACTIVATE);
}

static inline D2D1_COLOR_F d2col(uint32_t rgb, float a) { return D2D1_COLOR_F{((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, a}; }

// Direct2D path: everything into a premultiplied 32-bit DIB, then one BitBlt.
static bool aboutPaintD2D(HWND h, HDC target, int W, int H, float s) {
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = W; bi.bmiHeader.biHeight = -H;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) return false;
    HDC mdc = CreateCompatibleDC(nullptr);
    HGDIOBJ oldB = SelectObject(mdc, bmp);
    memset(bits, 0, (size_t)W * H * 4);

    D2D1_RENDER_TARGET_PROPERTIES props{};
    props.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
    props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    props.pixelFormat.alphaMode = g_aboutMica ? D2D1_ALPHA_MODE_PREMULTIPLIED : D2D1_ALPHA_MODE_IGNORE;
    props.dpiX = 96.f; props.dpiY = 96.f;
    props.usage = D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE;
    props.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;
    ID2D1DCRenderTarget* rt = nullptr;
    bool ok = false;
    if (SUCCEEDED(g_d2d->CreateDCRenderTarget(&props, &rt)) && rt) {
        RECT rc{0, 0, W, H};
        if (SUCCEEDED(rt->BindDC(mdc, &rc))) {
            const uint32_t fg = g_light ? 0x1A1A1A : 0xF0F0F0, lab = g_light ? 0x5E5E5E : 0x9E9E9E, bg = g_light ? 0xF3F3F3 : 0x202020;
            const uint32_t line = g_light ? 0x000000 : 0xFFFFFF;
            ID2D1SolidColorBrush *bFg = nullptr, *bLab = nullptr, *bFill = nullptr, *bStroke = nullptr;
            rt->CreateSolidColorBrush(d2col(fg, 1.f), &bFg);
            rt->CreateSolidColorBrush(d2col(lab, 1.f), &bLab);
            rt->CreateSolidColorBrush(d2col(line, g_light ? (g_aboutHover ? 0.09f : 0.045f) : (g_aboutHover ? 0.14f : 0.07f)), &bFill);
            rt->CreateSolidColorBrush(d2col(line, g_light ? 0.22f : 0.28f), &bStroke);
            rt->BeginDraw();
            rt->Clear(g_aboutMica ? D2D1_COLOR_F{0, 0, 0, 0} : d2col(bg, 1.f));
            rt->SetTextAntialiasMode(g_aboutMica ? D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE : D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
            auto drawText = [&](const std::wstring& t, IDWriteTextFormat* fmt, const RECT& r, ID2D1SolidColorBrush* brush, bool center) {
                if (t.empty() || !fmt) return;
                IDWriteTextLayout* lay = nullptr;
                if (FAILED(g_dw->CreateTextLayout(t.c_str(), (UINT32)t.size(), fmt, (float)std::max<LONG>(r.right - r.left, 1), (float)std::max<LONG>(r.bottom - r.top, 1), &lay)) || !lay) return;
                if (center) { lay->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER); lay->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER); }
                rt->DrawTextLayout(D2D1_POINT_2F{(float)r.left, (float)r.top}, lay, brush, D2D1_DRAW_TEXT_OPTIONS_NONE);
                lay->Release();
            };
            for (auto& r : g_aboutRows) {
                if (r.title) drawText(r.label, g_dwTitle, r.rv, bFg, false);
                else if (r.label.empty()) drawText(r.value, g_dwText, r.rv, bFg, false);
                else { drawText(r.label, g_dwLabel, r.rl, bLab, false); drawText(r.value, g_dwText, r.rv, bFg, false); }
            }
            D2D1_ROUNDED_RECT rr{D2D1_RECT_F{g_aboutBtn.left + 0.5f, g_aboutBtn.top + 0.5f, g_aboutBtn.right - 0.5f, g_aboutBtn.bottom - 0.5f}, 4.f * s, 4.f * s};
            rt->FillRoundedRectangle(&rr, bFill);
            rt->DrawRoundedRectangle(&rr, bStroke, 1.f);
            drawText(S(S_BTN_OK), g_dwText, g_aboutBtn, bFg, true);
            ok = SUCCEEDED(rt->EndDraw());
            if (bFg) bFg->Release(); if (bLab) bLab->Release(); if (bFill) bFill->Release(); if (bStroke) bStroke->Release();
        }
        rt->Release();
    }
    if (ok) BitBlt(target, 0, 0, W, H, mdc, 0, 0, SRCCOPY);
    SelectObject(mdc, oldB); DeleteDC(mdc); DeleteObject(bmp);
    return ok;
}
// GDI fallback: solid page, ClearType text straight onto the window.
static void aboutPaintGDI(HWND, HDC dc, int W, int H, float s) {
    RECT all{0, 0, W, H};
    HBRUSH bg = CreateSolidBrush(g_light ? RGB(0xF3, 0xF3, 0xF3) : RGB(0x20, 0x20, 0x20));
    FillRect(dc, &all, bg); DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ oldF = SelectObject(dc, g_aboutFont);
    for (auto& r : g_aboutRows) {
        SelectObject(dc, r.title ? g_aboutTitleFont : g_aboutFont);
        if (r.title || r.label.empty()) {
            SetTextColor(dc, g_light ? RGB(0x1A, 0x1A, 0x1A) : RGB(0xF0, 0xF0, 0xF0));
            RECT rv = r.rv; DrawTextW(dc, r.title ? r.label.c_str() : r.value.c_str(), -1, &rv, DT_NOPREFIX | DT_WORDBREAK);
        } else {
            SetTextColor(dc, g_light ? RGB(0x5E, 0x5E, 0x5E) : RGB(0x9E, 0x9E, 0x9E));
            RECT rl = r.rl; DrawTextW(dc, r.label.c_str(), -1, &rl, DT_NOPREFIX | DT_SINGLELINE);
            SetTextColor(dc, g_light ? RGB(0x1A, 0x1A, 0x1A) : RGB(0xF0, 0xF0, 0xF0));
            RECT rv = r.rv; DrawTextW(dc, r.value.c_str(), -1, &rv, DT_NOPREFIX | DT_WORDBREAK);
        }
    }
    SelectObject(dc, g_aboutFont);
    HPEN pen = CreatePen(PS_SOLID, 1, g_light ? RGB(0xC4, 0xC4, 0xC4) : RGB(0x5A, 0x5A, 0x5A));
    HBRUSH fill = CreateSolidBrush(g_light ? (g_aboutHover ? RGB(0xE4, 0xE4, 0xE4) : RGB(0xEC, 0xEC, 0xEC)) : (g_aboutHover ? RGB(0x3A, 0x3A, 0x3A) : RGB(0x2C, 0x2C, 0x2C)));
    HGDIOBJ oldP = SelectObject(dc, pen), oldBr = SelectObject(dc, fill);
    const int rr = (int)(8 * s);
    RoundRect(dc, g_aboutBtn.left, g_aboutBtn.top, g_aboutBtn.right, g_aboutBtn.bottom, rr, rr);
    SelectObject(dc, oldP); SelectObject(dc, oldBr); DeleteObject(pen); DeleteObject(fill);
    SetTextColor(dc, g_light ? RGB(0x1A, 0x1A, 0x1A) : RGB(0xF0, 0xF0, 0xF0));
    RECT br = g_aboutBtn;
    DrawTextW(dc, S(S_BTN_OK), -1, &br, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, oldF);
}
static void aboutPaint(HWND h, HDC target) {
    RECT cr; GetClientRect(h, &cr);
    const int W = cr.right, H = cr.bottom;
    if (W <= 0 || H <= 0) return;
    const float s = dpiOf(h) / 96.f;
    if (g_d2d && g_dw && aboutPaintD2D(h, target, W, H, s)) return;
    aboutPaintGDI(h, target, W, H, s);
}
static LRESULT CALLBACK AboutProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        g_aboutMica = false;
        if (d2dInit() && g_build >= 22621) {   // Mica only when we can render text properly onto it
            int backdrop = 2;   // DWMSBT_MAINWINDOW (Mica)
            MARGINS mg{-1, -1, -1, -1};
            if (SUCCEEDED(DwmExtendFrameIntoClientArea(h, &mg)) && SUCCEEDED(DwmSetWindowAttribute(h, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof backdrop))) g_aboutMica = true;
            else { MARGINS z{0, 0, 0, 0}; DwmExtendFrameIntoClientArea(h, &z); }
        }
        aboutLayout(h); aboutApplyTheme(h);
        return 0;
    }
    case WM_ERASEBKGND: {
        if (!g_aboutMica) return 1;   // the paint covers everything
        RECT r; GetClientRect(h, &r);
        FillRect((HDC)w, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));   // alpha 0 → Mica shows through
        return 1;
    }
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); aboutPaint(h, dc); EndPaint(h, &ps); return 0; }
    case WM_MOUSEMOVE: {
        POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        bool hov = PtInRect(&g_aboutBtn, pt) != 0;
        if (hov != g_aboutHover) { g_aboutHover = hov; InvalidateRect(h, nullptr, FALSE); }
        if (!g_aboutTracking) { TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, h, 0}; TrackMouseEvent(&t); g_aboutTracking = true; }
        return 0;
    }
    case WM_MOUSELEAVE: g_aboutTracking = false; if (g_aboutHover) { g_aboutHover = false; InvalidateRect(h, nullptr, FALSE); } return 0;
    case WM_LBUTTONUP: { POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)}; if (PtInRect(&g_aboutBtn, pt)) DestroyWindow(h); return 0; }
    case WM_KEYDOWN: if (w == VK_ESCAPE || w == VK_RETURN || w == VK_SPACE) DestroyWindow(h); return 0;
    case WM_DPICHANGED: aboutLayout(h); InvalidateRect(h, nullptr, TRUE); return 0;
    case WM_SETTINGCHANGE: case WM_THEMECHANGED: aboutApplyTheme(h); return 0;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY:
        g_hwndAbout = nullptr; g_aboutHover = g_aboutTracking = false;
        if (g_aboutFont) { DeleteObject(g_aboutFont); g_aboutFont = nullptr; }
        if (g_aboutTitleFont) { DeleteObject(g_aboutTitleFont); g_aboutTitleFont = nullptr; }
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static void aboutBuildRows() {
    g_aboutRows.clear();
    auto add = [](std::wstring label, std::wstring value, bool gapBefore = false, bool indent = false) {
        AboutRow r; r.label = std::move(label); r.value = std::move(value); r.gapBefore = gapBefore; r.indent = indent; g_aboutRows.push_back(r);
    };
    AboutRow t; t.title = true; t.label = std::wstring(g_zh ? L"栏声 Wavesill" : L"Wavesill") + L"  v" + WS_VERSION_W; g_aboutRows.push_back(t);
    add(L"", S(S_TAGLINE));
    std::wstring dev = g_capture.deviceName();
    if (dev.empty()) dev = S(S_NO_DEVICE);
    add(S(S_L_DEVICE), dev, true);
    add(S(S_L_RATE), std::to_wstring(g_capture.sampleRate()) + L" Hz");
    add(S(S_L_THEME), g_light ? S(S_THEME_LIGHT) : S(S_THEME_DARK));
    add(S(S_L_TRAY), g_trayIconSource.empty() ? std::wstring(S(S_TRAY_BUILTIN)) : std::wstring(S(S_TRAY_CUSTOM)) + L"  (" + g_trayIconSource + L")");
    wchar_t mod[160];
    switch (g_bridge.status) {
    case BR_CONNECTED:    _snwprintf_s(mod, 160, _TRUNCATE, L"%s  (v%u.%u.%u, Protocol %u)", S(S_MOD_CONNECTED), g_bridge.modVersion >> 16, (g_bridge.modVersion >> 8) & 255, g_bridge.modVersion & 255, g_bridge.seenProtocol); break;
    case BR_INCOMPATIBLE: _snwprintf_s(mod, 160, _TRUNCATE, L"%s  (Protocol %u; this build speaks %u)", S(S_MOD_INCOMPAT), g_bridge.seenProtocol, WS_BRIDGE_PROTOCOL); break;
    case BR_STALE:        _snwprintf_s(mod, 160, _TRUNCATE, L"%s", S(S_MOD_STALE)); break;
    default:              _snwprintf_s(mod, 160, _TRUNCATE, L"%s", S(S_MOD_NONE)); break;
    }
    add(S(S_L_MOD), mod);
    if (g_bridge.status == BR_CONNECTED) {
        const uint32_t f = g_bridge.stageFlags();
        int top = -1; for (int b = 0; b < (int)WS_STAGE_COUNT; ++b) if (f & (1u << b)) top = b;
        wchar_t st[160];
        if (f & WS_STAGE_GUARD) _snwprintf_s(st, 160, _TRUNCATE, L"%s", S(S_STAGE_GUARD));
        else if (f & WS_STAGE_OFF) _snwprintf_s(st, 160, _TRUNCATE, L"%s", S(S_STAGE_OFF));
        else if (top < 0) _snwprintf_s(st, 160, _TRUNCATE, L"0/%u  %s", WS_STAGE_COUNT, S(S_STAGE_WAIT));
        else _snwprintf_s(st, 160, _TRUNCATE, L"%d/%u  %s", top + 1, WS_STAGE_COUNT, S((StrId)(S_STAGE_0 + top)));
        add(S(S_L_STAGE), st);
    }
    add(S(S_L_TASKBARS), L"", true);
    if (g_overlays.empty()) add(L"", L"-", false, true);
    for (auto& o : g_overlays) {
        const MonitorPrefs& p = monitorPrefs(o.monDev);
        const wchar_t* via = !p.enabled ? S(S_VIA_OFF) : o.viaMod ? S(S_VIA_MOD) : o.shown ? S(S_VIA_OVERLAY) : S(S_VIA_HIDDEN);
        add(L"", overlayLabel(o) + L"   [" + via + L"]", false, true);
    }
    add(S(S_L_SETTINGS), g_iniPath, true);
    {
        const char* st = WS_BUILD_STAMP; std::wstring w; for (const char* c = st; *c; ++c) w.push_back((wchar_t)(unsigned char)*c);
        // architecture, and whether this build is being emulated (an x64 exe on an ARM64 PC)
        USHORT pm = 0, nm = 0;
        typedef BOOL (WINAPI* Wow2Fn)(HANDLE, USHORT*, USHORT*);
        auto wow2 = (Wow2Fn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2");
        if (wow2) wow2(GetCurrentProcess(), &pm, &nm);
        auto name = [](USHORT m) -> const wchar_t* { return m == 0x8664 ? L"x64" : m == 0xAA64 ? L"ARM64" : m == 0x014C ? L"x86" : L"?"; };
#if defined(_M_ARM64) || defined(__aarch64__)
        w += L"  ·  ARM64";
#elif defined(_M_X64) || defined(__x86_64__)
        w += L"  ·  x64";
#else
        w += L"  ·  x86";
#endif
        if (pm != 0 && nm != 0 && pm != nm) { w += L"  ("; w += S(S_EMULATED); w += L" "; w += name(nm); w += L")"; }
        add(S(S_L_BUILD), w);
    }
    add(S(S_L_PROJECT), L"github.com/GenuineHorace/Wavesill");
}
static void showAbout() {
    aboutBuildRows();
    if (g_hwndAbout) { aboutLayout(g_hwndAbout); InvalidateRect(g_hwndAbout, nullptr, TRUE); SetForegroundWindow(g_hwndAbout); return; }
    g_hwndAbout = CreateWindowExW(WS_EX_TOPMOST, ABOUT_CLASS, S(S_ABOUT_TITLE), WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, 0, 0, 10, 10, nullptr, nullptr, g_hInst, nullptr);
    if (g_hwndAbout) { ShowWindow(g_hwndAbout, SW_SHOW); SetForegroundWindow(g_hwndAbout); SetFocus(g_hwndAbout); }
}

// --------------------------------------------------------------------------- tray menu
enum {
    IDM_HEADER = 1, IDM_PAUSE, IDM_AUTOSTART, IDM_ABOUT, IDM_EXIT, IDM_CUSTOMTRAY, IDM_OPENFOLDER,
    IDM_G_STYLE = 100, IDM_G_OPACITY = 110, IDM_G_FALL = 120, IDM_G_COLOR = 130, IDM_G_FADE = 140,
    IDM_M_BASE = 1000, IDM_M_STRIDE = 100, IDM_M_OVERRIDE = 0, IDM_M_ENABLED = 1, IDM_M_STYLE = 10, IDM_M_OPACITY = 20, IDM_M_FALL = 30, IDM_M_COLOR = 40, IDM_M_FADE = 50,
};
static void visualMenus(UINT idStyle, UINT idFade, UINT idColor, UINT idOpacity, UINT idFall, const Visual& v, HMENU parent) {
    HMENU st = CreatePopupMenu(), co = CreatePopupMenu(), op = CreatePopupMenu(), fa = CreatePopupMenu();
    static const StrId styleNames[ST_COUNT] = {S_ST_BARS, S_ST_FILLED, S_ST_LINE};
    static const StrId colorNames[COL_COUNT] = {S_COL_WHITE, S_COL_BLACK, S_COL_THEME};
    for (int i = 0; i < ST_COUNT; ++i) AppendMenuW(st, MF_STRING, idStyle + i, S(styleNames[i]));
    CheckMenuRadioItem(st, idStyle, idStyle + ST_COUNT - 1, idStyle + v.style, MF_BYCOMMAND);
    AppendMenuW(st, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(st, MF_STRING | (v.fade ? MF_CHECKED : 0), idFade, S(S_FADE));
    for (int i = 0; i < COL_COUNT; ++i) AppendMenuW(co, MF_STRING, idColor + i, S(colorNames[i]));
    CheckMenuRadioItem(co, idColor, idColor + COL_COUNT - 1, idColor + v.color, MF_BYCOMMAND);
    for (int i = 0; i < cfg::OPACITY_COUNT; ++i) {
        wchar_t t[48]; _snwprintf_s(t, 48, _TRUNCATE, L"%d%%%s", cfg::OPACITY_PCT[i], i == cfg::OPACITY_DEFAULT ? S(S_DEFAULT_TAG) : L"");
        AppendMenuW(op, MF_STRING, idOpacity + i, t);
    }
    CheckMenuRadioItem(op, idOpacity, idOpacity + cfg::OPACITY_COUNT - 1, idOpacity + v.opacity, MF_BYCOMMAND);
    AppendMenuW(fa, MF_STRING, idFall + FALL_INSTANT, S(S_FALL_INSTANT));
    AppendMenuW(fa, MF_STRING, idFall + FALL_PEAK_HOLD, S(S_FALL_PEAK));
    CheckMenuRadioItem(fa, idFall, idFall + FALL_COUNT - 1, idFall + v.fall, MF_BYCOMMAND);
    AppendMenuW(parent, MF_POPUP, (UINT_PTR)st, S(S_STYLE));
    AppendMenuW(parent, MF_POPUP, (UINT_PTR)co, S(S_COLOR));
    AppendMenuW(parent, MF_POPUP, (UINT_PTR)op, S(S_OPACITY));
    AppendMenuW(parent, MF_POPUP, (UINT_PTR)fa, S(S_FALL));
}
static void showTrayMenu() {
    static bool inMenu = false;
    if (inMenu) return;
    inMenu = true;
    refreshCustomTrayAvailability();

    HMENU m = CreatePopupMenu();
    wchar_t header[64]; _snwprintf_s(header, 64, _TRUNCATE, g_zh ? L"栏声 Wavesill v%s" : L"Wavesill v%s", WS_VERSION_W);
    AppendMenuW(m, MF_STRING | MF_GRAYED, IDM_HEADER, header);
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    visualMenus(IDM_G_STYLE, IDM_G_FADE, IDM_G_COLOR, IDM_G_OPACITY, IDM_G_FALL, g_settings.global, m);
    const int nMon = std::min((int)g_overlays.size(), 8);
    if (nMon > 1) {
        HMENU pm = CreatePopupMenu();
        for (int i = 0; i < nMon; ++i) {
            const Overlay& o = g_overlays[i];
            const MonitorPrefs& p = monitorPrefs(o.monDev);
            const UINT base = IDM_M_BASE + i * IDM_M_STRIDE;
            HMENU sub = CreatePopupMenu();
            AppendMenuW(sub, MF_STRING | (p.enabled ? MF_CHECKED : 0), base + IDM_M_ENABLED, S(S_SHOW_HERE));
            AppendMenuW(sub, MF_STRING | (p.override_ ? MF_CHECKED : 0), base + IDM_M_OVERRIDE, S(S_OVERRIDE));
            AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
            visualMenus(base + IDM_M_STYLE, base + IDM_M_FADE, base + IDM_M_COLOR, base + IDM_M_OPACITY, base + IDM_M_FALL, p.override_ ? p.v : g_settings.global, sub);
            AppendMenuW(pm, MF_POPUP, (UINT_PTR)sub, overlayLabel(o).c_str());
        }
        AppendMenuW(m, MF_POPUP, (UINT_PTR)pm, S(S_PER_MONITOR));
    }
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    if (g_customTrayAvailable) AppendMenuW(m, MF_STRING | (g_settings.customTray ? MF_CHECKED : 0), IDM_CUSTOMTRAY, S(S_CUSTOM_TRAY));
    AppendMenuW(m, MF_STRING | (g_settings.paused ? MF_CHECKED : 0), IDM_PAUSE, S(S_PAUSE));
    AppendMenuW(m, MF_STRING | (autostartEnabled() ? MF_CHECKED : 0), IDM_AUTOSTART, S(S_AUTOSTART));
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, IDM_OPENFOLDER, S(S_OPEN_FOLDER));
    AppendMenuW(m, MF_STRING, IDM_ABOUT, S(S_ABOUT));
    AppendMenuW(m, MF_STRING, IDM_EXIT, S(S_EXIT));

    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(g_hwndMain);
    UINT cmd = (UINT)TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, g_hwndMain, nullptr);
    PostMessageW(g_hwndMain, WM_NULL, 0, 0);
    DestroyMenu(m);
    inMenu = false;

    Visual& g = g_settings.global;
    if (cmd >= IDM_G_STYLE && cmd < IDM_G_STYLE + ST_COUNT)             { g.style = (int)(cmd - IDM_G_STYLE); settingsSaveGlobal(); redrawAll(); return; }
    if (cmd == IDM_G_FADE)                                              { g.fade = !g.fade; settingsSaveGlobal(); redrawAll(); return; }
    if (cmd >= IDM_G_COLOR && cmd < IDM_G_COLOR + COL_COUNT)            { g.color = (int)(cmd - IDM_G_COLOR); settingsSaveGlobal(); redrawAll(); return; }
    if (cmd >= IDM_G_OPACITY && cmd < IDM_G_OPACITY + cfg::OPACITY_COUNT) { g.opacity = (int)(cmd - IDM_G_OPACITY); settingsSaveGlobal(); redrawAll(); return; }
    if (cmd >= IDM_G_FALL && cmd < IDM_G_FALL + FALL_COUNT)             { g.fall = (int)(cmd - IDM_G_FALL); settingsSaveGlobal(); redrawAll(); return; }
    if (cmd >= IDM_M_BASE && cmd < IDM_M_BASE + 8 * IDM_M_STRIDE) {
        const int i = (int)(cmd - IDM_M_BASE) / IDM_M_STRIDE, off = (int)(cmd - IDM_M_BASE) % IDM_M_STRIDE;
        if (i < (int)g_overlays.size()) {
            Overlay& o = g_overlays[i];
            MonitorPrefs& p = monitorPrefs(o.monDev);
            if (off == IDM_M_OVERRIDE) { p.override_ = !p.override_; if (p.override_) p.v = g_settings.global; }
            else if (off == IDM_M_ENABLED) p.enabled = !p.enabled;
            else {
                if (!p.override_) { p.override_ = true; p.v = g_settings.global; }
                if (off >= IDM_M_STYLE && off < IDM_M_STYLE + ST_COUNT) p.v.style = off - IDM_M_STYLE;
                else if (off == IDM_M_FADE) p.v.fade = !p.v.fade;
                else if (off >= IDM_M_OPACITY && off < IDM_M_OPACITY + cfg::OPACITY_COUNT) p.v.opacity = off - IDM_M_OPACITY;
                else if (off >= IDM_M_FALL && off < IDM_M_FALL + FALL_COUNT) p.v.fall = off - IDM_M_FALL;
                else if (off >= IDM_M_COLOR && off < IDM_M_COLOR + COL_COUNT) p.v.color = off - IDM_M_COLOR;
            }
            monitorPrefsSave(o.monDev);
            o.needPresent = true; o.blank = false;
        }
        return;
    }
    switch (cmd) {
    case IDM_CUSTOMTRAY: g_settings.customTray = !g_settings.customTray; settingsSaveGlobal(); trayUpdateIcon(); break;
    case IDM_OPENFOLDER: ShellExecuteW(nullptr, L"open", g_dataDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
    case IDM_PAUSE:      g_settings.paused = !g_settings.paused; break;
    case IDM_AUTOSTART:  autostartSet(!autostartEnabled()); break;
    case IDM_ABOUT:      showAbout(); break;
    case IDM_EXIT:       DestroyWindow(g_hwndMain); break;
    }
}

// --------------------------------------------------------------------------- frame loop
static Spectrum g_spectrum;
static float g_samples[Spectrum::N];
static float g_db[Spectrum::BINS];
static float g_ceilDb = cfg::CEIL_MIN_DB;
static LARGE_INTEGER g_qpf{}, g_lastTick{};
static ULONGLONG g_lastScan = 0;
static bool g_forceScan = true;

static void onThemeChanged() {
    applyMenuTheme(g_light);
    trayUpdateIcon();
    if (g_hwndAbout) aboutApplyTheme(g_hwndAbout);
    redrawAll();
}
static void frame() {
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    float dt = (float)(now.QuadPart - g_lastTick.QuadPart) / (float)g_qpf.QuadPart;
    g_lastTick = now;
    dt = std::min(std::max(dt, 0.001f), 0.1f);
    const ULONGLONG ms = GetTickCount64();

    if (g_forceScan || ms - g_lastScan >= (ULONGLONG)cfg::TASKBAR_SCAN_MS) {
        g_forceScan = false; g_lastScan = ms;
        refreshTaskbars();
        bridgeMaintain(ms);
        bool light = readLightTheme();
        if (light != g_light) { g_light = light; onThemeChanged(); }
    }

    // ---- analysis (once per frame, shared by all taskbars)
    const int sr = g_capture.sampleRate();
    g_ring.latest(g_samples, Spectrum::N);
    g_spectrum.compute(g_samples, g_db, sr);
    const float binHz = (float)sr / Spectrum::N;
    const float fMax = std::min(cfg::FREQ_MAX, 0.45f * sr);
    float peak = -200.f;
    {
        const int k0 = std::max(1, (int)(cfg::FREQ_MIN / binHz)), k1 = std::min(Spectrum::BINS - 1, (int)(fMax / binHz));
        for (int k = k0; k <= k1; ++k) peak = std::max(peak, g_db[k]);
    }
    g_ceilDb = std::max(peak, g_ceilDb - cfg::CEIL_RELEASE * dt);
    g_ceilDb = std::min(std::max(g_ceilDb, cfg::CEIL_MIN_DB), cfg::TILT_MAX_DB);
    const float floorDb = g_ceilDb - cfg::RANGE_DB;

    // ---- per taskbar
    for (auto& o : g_overlays) {
        if (!overlayUpdateGeometry(o)) { o.alive = false; g_forceScan = true; continue; }
        const bool wantHidden = g_settings.paused || !monitorPrefs(o.monDev).enabled || overlayShouldHide(o);
        if (wantHidden) { o.viaMod = false; bridgePush(o, false, ms); overlayHide(o); continue; }
        overlayFrame(o, g_db, Spectrum::BINS, binHz, floorDb, dt);
        o.viaMod = bridgePush(o, !o.blank, ms);
        if (o.viaMod) { overlayHide(o); continue; }
        if (o.needPresent) overlayPresent(o);
        if (o.shown && ++o.zTick >= cfg::ZORDER_EVERY_N) { o.zTick = 0; overlayEnsureTop(o); }
    }
}

// --------------------------------------------------------------------------- main window
static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == g_msgTaskbarCreated && g_msgTaskbarCreated) { trayAdd(); g_forceScan = true; return 0; }
    switch (m) {
    case WM_TIMER: frame(); return 0;
    case WM_TRAY:
        switch (LOWORD(l)) { case WM_CONTEXTMENU: case NIN_SELECT: case NIN_KEYSELECT: showTrayMenu(); break; }
        return 0;
    case WM_SETTINGCHANGE: case WM_DISPLAYCHANGE: case WM_THEMECHANGED: case WM_DPICHANGED: g_forceScan = true; return 0;
    case WM_QUERYENDSESSION: return TRUE;
    case WM_ENDSESSION: if (w) trayRemove(); return 0;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    g_hInst = hInst;
    enablePerMonitorDpi();
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\Wavesill.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    g_build = windowsBuild(); g_win11 = g_build >= 22000;
    g_zh = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE;
    initDataFolder();
    settingsLoad();
    refreshCustomTrayAvailability();
    g_light = readLightTheme();
    initDarkModeApis();
    applyMenuTheme(g_light);
    QueryPerformanceFrequency(&g_qpf); QueryPerformanceCounter(&g_lastTick);
    g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSW wc{};
    wc.lpfnWndProc = OverlayProc; wc.hInstance = hInst; wc.lpszClassName = OVERLAY_CLASS; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    wc.lpfnWndProc = MainProc; wc.lpszClassName = MAIN_CLASS;
    RegisterClassW(&wc);
    wc.lpfnWndProc = AboutProc; wc.lpszClassName = ABOUT_CLASS; wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APP));
    RegisterClassW(&wc);

    g_hwndMain = CreateWindowExW(WS_EX_TOOLWINDOW, MAIN_CLASS, L"Wavesill", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, hInst, nullptr);
    if (!g_hwndMain) return 1;

    trayAdd();
    g_capture.start();
    SetTimer(g_hwndMain, 1, cfg::FRAME_MS, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }

    KillTimer(g_hwndMain, 1);
    g_capture.stop();
    bridgeClose();
    for (auto& o : g_overlays) overlayDestroy(o);
    g_overlays.clear();
    trayRemove();
    if (mutex) CloseHandle(mutex);
    return 0;
}
