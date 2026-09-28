# Wavesill Behind Taskbar Content — installation and troubleshooting

[中文](MOD-GUIDE.zh-CN.md)

The mod puts the spectrum *inside* the Windows 11 taskbar: beneath the Start button, app icons, tray and clock, above the acrylic backdrop. Windows 11 only, x64 and ARM64.

## 1. Run Wavesill first

Start `Wavesill.exe` (or `Wavesill-arm64.exe`), 0.6.2 or later. In the tray menu → About…, "Windhawk Mod" reads "Not detected" and every taskbar line ends with `[Overlay]`. That is the starting point.

## 2. Install the mod

**From the Windhawk repository** (once it is published there): open Windhawk → Explore → search for *Wavesill Behind Taskbar Content* → Install.

**From this repository**: open Windhawk → Home → **Create a new mod**. Windhawk opens its built-in source editor with a template. Replace the whole template with the contents of `mod/wavesill-behind-taskbar-content.wh.cpp` and click **Compile**. The mod is compiled on your machine and enabled when it succeeds.

## 3. Check that it works

Within about five seconds:

- The spectrum that used to float on the taskbar disappears and reappears behind the icons.
- Wavesill's About window shows **Windhawk Mod: Connected (v0.6.3, Protocol 1)**, **Mod Stage: 9/9 Frames flowing**, and `[Mod]` on each taskbar line.

If it does not:

1. Read the **Mod Stage** line first; it names the last step that succeeded. The nine steps are: XAML loaded → Diagnostics attached → Tree subscribed → Background element found → Element inserted → Timer created → Ticking → Taskbar matched → Frames flowing.
2. Open the mod's page in Windhawk → Advanced → Debug logging, then disable and re-enable the mod with the log open. A healthy run logs:
   ```
   Wavesill Behind Taskbar Content 0.6.3: init
   Bridge ready: 4 slots, ... bytes
   InitializeXamlDiagnosticsEx: 0x00000000
   AdviseVisualTreeChange: 0x00000000
   TaskbarBackground found (1 so far)
   Spectrum element inserted at index 1 of Windows.UI.Xaml.Controls.Grid (3 children)
   Timer started (queue yes, dispatcher no)
   First tick
   First frame copied: 1920 × 48 px, image 1920.0 × 48.0 DIP
   ```
3. Report the stage line and the log in an [issue](https://github.com/GenuineHorace/Wavesill/issues).

## 4. If Explorer crashes

The mod carries a crash guard. It leaves a marker while asking XAML to load it and clears the marker after 20 stable seconds. If Explorer dies in between, the next run skips the XAML part and stays inert; About shows "Skipped: The previous attach crashed Explorer". Your PC keeps working. To try again, disable and re-enable the mod.

## 5. Disabling, removing, updating

- Disable the mod in Windhawk: Wavesill's overlay returns within a second; nothing needs restarting. Re-enabling wakes the mod up again.
- Remove it: mod page → Remove.
- A disabled mod's DLL stays in Explorer until Explorer restarts (the XAML diagnostics API never releases it). This is harmless and uses almost no memory.
- Update: mod page → Advanced → Edit mod source, paste the new file, Compile. The version is in the `@version` line at the top and is kept in step with Wavesill's.

## 6. What it changes

Two things, without hooking any function:

1. Through Windows' public XAML diagnostics API (the one Visual Studio's Live Visual Tree, TranslucentTB and the Taskbar Styler use) it watches the taskbar's XAML tree, finds the `Taskbar.TaskbarBackground` element and inserts one `Image` right after it.
2. Every frame it copies the picture Wavesill has already rendered from the shared-memory block `Local\Wavesill.Bridge` into that `Image`.

Styles, colours, opacity and per-monitor settings are all Wavesill's business; the mod never needs to change for them.
