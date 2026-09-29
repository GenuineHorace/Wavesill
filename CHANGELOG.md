# Changelog

Versions follow `major.minor.patch`. The Windhawk mod has its own version number, bumped only when the mod file changes; the two sides only have to agree on the bridge protocol number (see BRIDGE.md), which the About window shows. When bumping Wavesill, update `src/version.h`, `src/wavesill.manifest` and this file; when bumping the mod, its `@version`, `MOD_VERSION_U32`, the init log line and MOD-GUIDE.

## v0.6.4 — 2026-09-29

**Wavesill**

- Filled and Line styles: smooth edges. The curve is sampled four times per pixel column and every edge pixel gets its exact coverage, so the top of the filled area, the line and the peak-hold line no longer show steps on slopes. (The filled area had no anti-aliasing at all; the line was only anti-aliased vertically.)
- The x64 build is now published as `Wavesill-x64.exe`, alongside `Wavesill-arm64.exe`.

**Wavesill Behind Taskbar Content (Windhawk mod)**

- Unchanged (0.6.3).

## v0.6.3 — 2026-09-29

**Wavesill Behind Taskbar Content (Windhawk mod)**

- Compiler options trimmed to the three libraries the mod links: no `-std` override (Windhawk's default standard is newer) and no `--export-all-symbols` (the two COM entry points are exported explicitly). No change in behaviour.

**Wavesill**

- Version number only, to stay in step with the mod.

## v0.6.2 — 2026-09-28

First public release.

**Wavesill**

- WASAPI loopback capture of whatever the default output device is playing; automatic reopen on device change, unplug or Explorer restart.
- 2048-point Hann-windowed FFT, 35 Hz–16 kHz log-spaced bands, slow automatic gain, 4 dB/octave treble tilt.
- One per-pixel-alpha, click-through, always-on-top layer per taskbar, following the taskbar's rectangle, DPI and edge (bottom, top, left, right) every frame; hidden with an auto-hidden taskbar or behind a full-screen window; keeps the Windows 11 taskbar's top border clear.
- Styles: Bars, Filled, Line, each with an optional fade toward the taskbar edge. Colours: Follow Theme, White, Black. Six opacity steps. Fall: Instant or Peak Hold. Per-monitor overrides.
- Tray menu and About window follow the system theme (dark menu, Mica on Windows 11 22H2+, DirectWrite text); English and Chinese UI by system language.
- Settings in `%LOCALAPPDATA%\Wavesill\settings.ini`; optional custom tray icons in `TrayIcons\`; only "Start with Windows" touches the registry.
- x64 and ARM64 builds, cross-compiled with Zig; MSVC build script included.

**Wavesill Behind Taskbar Content (Windhawk mod)**

- Displays Wavesill's frames inside the Windows 11 taskbar, beneath the icons and above the backdrop, through the public XAML diagnostics API; no function hooks.
- Shared-memory bridge (protocol 1) with two-way heartbeats: the overlay takes over within a second when the mod is disabled or breaks.
- Crash guard, progress reporting ("Mod Stage" in Wavesill's About window), sleep/wake across enable cycles, an "Attach to the Taskbar" setting. x64 and ARM64.
