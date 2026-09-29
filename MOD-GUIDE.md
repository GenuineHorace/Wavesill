# Wavesill Behind Taskbar Content — installation and troubleshooting

[中文](MOD-GUIDE.zh-CN.md)

The mod puts the spectrum *inside* the Windows 11 taskbar: beneath the Start button, app icons, tray and clock, above the acrylic backdrop. Windows 11 only, x64 and ARM64.

## 1. Run Wavesill first

Start `Wavesill-x64.exe` (or `Wavesill-arm64.exe`), 0.6.2 or later. In the tray menu → About…, "Windhawk Mod" reads "Not detected" and every taskbar line ends with `[Overlay]`. That is the starting point.

## 2. Install the mod

**From the Windhawk repository** (once it is published there): open Windhawk → Explore → search for *Wavesill Behind Taskbar Content* → Install.

**From this repository**: open Windhawk → Home → **Create a new mod**. Windhawk opens its built-in source editor with a template. Replace the whole template with the contents of `mod/wavesill-behind-taskbar-content.wh.cpp` and click **Compile**. The mod is compiled on your machine and enabled when it succeeds.

The first time the mod is enabled, Windhawk downloads a few MB of Windows symbols for `taskbar.dll` (the mod's status reads "Loading symbols…"). That takes up to a minute and needs an internet connection; afterwards the symbols are cached until the next Windows update replaces the file.

## 3. Check that it works

Within about five seconds:

- The spectrum that used to float on the taskbar disappears and reappears behind the icons.
- Wavesill's About window shows **Windhawk Mod: Connected (v0.7.0, Protocol 1)**, **Mod Stage: 9/9 Frames flowing**, and `[Mod]` on each taskbar line.

If it does not:

1. Read the **Mod Stage** line first; it names the last step that succeeded. The nine steps are: Symbols resolved → Taskbar thread reached → Timer created → Ticking → XAML root found → Background element found → Element inserted → Taskbar matched → Frames flowing.
2. Open the mod's page in Windhawk → Advanced → Debug logging, then disable and re-enable the mod with the log open. A healthy run logs:
   ```
   Wavesill Behind Taskbar Content 0.7.0: init
   Bridge ready: 4 slots, ... bytes
   Timer started on the taskbar thread
   First tick
   Spectrum element inserted at index 1 of Windows.UI.Xaml.Controls.Grid in primary taskbar ... (1 taskbars)
   First frame copied: 1920 × 48 px, image 1920.0 × 48.0 DIP
   ```
3. Report the stage line and the log in an [issue](https://github.com/GenuineHorace/Wavesill/issues).

## 4. If the mod fails to load

Windhawk shows the mod as failed when the `taskbar.dll` symbols cannot be resolved: no internet connection on first use, or a Windows update that renamed something. Wavesill's overlay keeps working meanwhile. Retry after checking the connection; if it keeps failing after a Windows update, report the Windows build in an issue.

## 5. Disabling, removing, updating

- Disable the mod in Windhawk: the element is taken out of the taskbar, the mod unloads, and Wavesill's overlay returns within a second. Nothing needs restarting.
- Remove it: mod page → Remove.
- Update: mod page → Advanced → Edit mod source, paste the new file, Compile. The version is in the `@version` line at the top; it changes only when the mod itself changes, so it need not match Wavesill's.

## 6. What it changes

Two things, without hooking any function:

1. It reaches each taskbar's XAML tree from the taskbar window, on the taskbar's own thread, through a few `taskbar.dll` symbols (the way the other taskbar mods do), finds the `Taskbar.TaskbarBackground` element and inserts one `Image` right after it.
2. Every frame it copies the picture Wavesill has already rendered from the shared-memory block `Local\Wavesill.Bridge` into that `Image`.

Styles, colours, opacity and per-monitor settings are all Wavesill's business; the mod never needs to change for them.
