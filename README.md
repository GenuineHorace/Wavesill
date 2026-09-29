# Wavesill

**A faint audio spectrum that lives on the Windows taskbar.**

[中文](README.zh-CN.md) · [Windhawk mod](#windhawk-mod-optional-windows-11)

Wavesill turns whatever Windows is playing right now (music, video, a notification sound) into a very faint, live spectrum that spans the whole taskbar. It is fully click-through, follows the taskbar's position, height, DPI and theme, and fades away when the system is silent. Single-file C++/Win32, no runtime dependencies, about 500 KB.

## Run

1. Download the exe that matches your machine from [Releases](https://github.com/GenuineHorace/Wavesill/releases): `Wavesill-x64.exe` (x64) or `Wavesill-arm64.exe` (ARM64). Windows 10 1703 or later, or Windows 11. No installer, no runtime to install. (The other one also runs, through emulation; the About window's "Build" line tells you which architecture is running.)
2. A tray icon appears. Play anything and a faint spectrum appears on the taskbar.
3. Right-click (or left-click) the tray icon:

   | Item | What it does |
   |---|---|
   | **Style** | Bars / Filled / Line, plus an independent "Fade Toward the Edge" toggle that applies to any of the three |
   | **Color** | Follow Theme (default: white on a dark taskbar, black on a light one) / White / Black |
   | **Opacity** | 4 % / 7 % / 10 % (default) / 14 % / 20 % / 30 % |
   | **Fall** | Instant / Peak Hold (the tip holds for 0.3 s, then falls with gravity) |
   | **Per Monitor** | Only shown with two or more taskbars: each taskbar can have its own style, colour, opacity and fall, or be switched off |
   | **Custom Tray Icon** | Only shown when a file exists in `TrayIcons` (see below) |
   | **Pause** / **Start with Windows** | |
   | **Open Data Folder** | Opens `%LOCALAPPDATA%\Wavesill` |
   | **About…** | Version, build, audio device and sample rate, theme, tray icon source, Windhawk mod status and stage, and every taskbar's size, edge and who is drawing it |
   | **Exit** | |

   The tray icon, the menu and the About window follow the system theme; on Windows 11 22H2 or later the About window uses Mica.

SmartScreen may stop an unsigned exe the first time: click "More info → Run anyway", or build it yourself (below).

## Data folder

Everything Wavesill keeps lives in `%LOCALAPPDATA%\Wavesill\`:

```
settings.ini        All settings. Human-readable; edit it while Wavesill is not running; delete it to reset.
TrayIcons\          Optional custom tray icons
  tray_dark.ico       Used when the taskbar is dark
  tray_light.ico      Used when the taskbar is light
  tray.ico            Used for both when the matching file above is missing
  README.txt          Same notes
```

Deleting that folder and turning off "Start with Windows" is a clean uninstall. "Start with Windows" is the only thing written to the registry (`HKCU\Software\Microsoft\Windows\CurrentVersion\Run\Wavesill`), because that is where Windows keeps the list.

One setting has no menu item: `BorderInset`. The Windows 11 taskbar draws a 1-DIP border along its top edge; by default (`auto`) Wavesill keeps 1 px (scaled by DPI) clear on Windows 11 and 0 px on Windows 10. Set it to a pixel count at 96 DPI to override.

## What it handles by itself

| Situation | Behaviour |
|---|---|
| Taskbar at the bottom, top, left or right | Bars grow from the screen edge (sideways on a vertical taskbar) |
| Several monitors and taskbars | One layer per taskbar (`Shell_TrayWnd` and every `Shell_SecondaryTrayWnd`) |
| Resolution or DPI change, taskbar resized or moved to another edge | The taskbar's real rectangle and DPI are read every frame; nothing drifts |
| Auto-hide taskbar | Slides away and comes back with it |
| A full-screen game, video or presentation | Hidden while a full-screen window owns that monitor |
| Dark / light theme | Icon, menu and About window switch; the spectrum follows the Color setting |
| The taskbar climbing above the layer | Z-order is checked regularly and restored |
| Default output device changed, unplugged or reset | The capture thread reopens the new default device |
| Explorer restarted | Taskbars are found again, layers rebuilt, the tray icon re-added |
| Silence | Bars decay to zero, then drawing stops (near-zero CPU) |
| Very quiet audio | A slow automatic gain keeps quiet notifications visible and loud music readable |
| Started twice | Single instance; the second start exits quietly |
| The Windhawk mod is running | See below |

## Windhawk mod (optional, Windows 11)

An overlay can only ever float *on top of* the taskbar. To put the spectrum truly *inside* it, beneath the icons and above the acrylic backdrop, something has to run inside Explorer, and that is a job for a [Windhawk](https://windhawk.net) mod. The mod reaches each taskbar's XAML tree from its window and inserts one image element beneath the icons; it hooks no functions. Wavesill itself never injects into any process; it just publishes each finished frame into a small shared-memory block (`Local\Wavesill.Bridge`). When the mod is present it displays the frames inside the taskbar; when it is absent, disabled or broken by a Windows update, the overlay takes over again within a second.

The mod is `mod/wavesill-behind-taskbar-content.wh.cpp` and is also published in the Windhawk mod repository as **Wavesill Behind Taskbar Content**. Installation and troubleshooting: **[MOD-GUIDE.md](MOD-GUIDE.md)**. The protocol between the two: [BRIDGE.md](BRIDGE.md) and `src/wavesill_bridge.h`. Windows 11 only, x64 and ARM64.

## Visuals

- Bands: 35 Hz to 16 kHz, log-spaced, one band per 4 px (scaled by DPI); in the Bars style each bar is 3 px wide with a 1 px gap.
- **Bars**: the tip row is a little brighter. **Filled**: a continuous filled area. **Line**: a 1.5 px smooth outline. Both are anti-aliased: the curve is sampled four times per pixel column and edge pixels get their exact coverage. **Fade Toward the Edge**: bars, the filled area or the area below the line fade to 30 % at the taskbar edge.
- Fast attack (30 ms), slow release (140 ms), light smoothing between neighbouring bands (twice for the curve styles).
- **Peak Hold**: the tip holds for 0.3 s, then falls with an acceleration of 3 screen heights per s².
- A 4 dB/octave tilt above 250 Hz, because music has far less energy in the treble; without it the right half would always be flat.

All of these live in `namespace cfg` at the top of `src/wavesill.cpp`.

## Build

One `.cpp`, one `.rc`, two headers, no third-party libraries.

```
build.cmd                                Visual Studio (x64 Native Tools Command Prompt)
./build.sh                               Zig, cross-compiles from any OS: pip install ziglang
TARGET=aarch64-windows-gnu ./build.sh    ARM64 with Zig
build-zig.cmd                            Zig on Windows
./build-release.sh                       Both architectures into dist/
tools/make_icons.py                      Regenerates the icons (Python 3 + Pillow)
```

The release binaries are built with Zig: static libc++, only system DLLs imported. The GitHub Actions workflow builds both architectures on every push and attaches them to a release when a `v*` tag is pushed.

## Known limitations

- WASAPI loopback cannot capture exclusive-mode output (ASIO, WASAPI Exclusive).
- The dark menu relies on undocumented uxtheme ordinals (135/136/104); if a future Windows changes them the menu falls back to light, nothing else is affected.

## Files

```
src/wavesill.cpp            Everything
src/wavesill_bridge.h       Shared-memory protocol with the Windhawk mod (C, used verbatim by both sides)
src/version.h               The version number (the only place to change it)
src/wavesill.rc             Icons, version info, manifest
src/wavesill.manifest       PerMonitorV2 DPI awareness
src/wavesill.ico            Exe / About icon
src/tray_white.ico          Built-in tray icon (dark taskbar)
src/tray_black.ico          Built-in tray icon (light taskbar)
mod/wavesill-behind-taskbar-content.wh.cpp   Windhawk mod (single file, includes a copy of the protocol)
tools/make_icons.py         Icon generator
BRIDGE.md                   Protocol notes
MOD-GUIDE.md                Mod installation and troubleshooting
*.zh-CN.md                  Chinese versions of the three documents above
CHANGELOG.md
```

MIT License
