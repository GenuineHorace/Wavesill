# Wavesill Bridge — the protocol between Wavesill and the Windhawk mod

[中文](BRIDGE.zh-CN.md)

`src/wavesill_bridge.h` is the authoritative definition; the mod carries a verbatim copy because a Windhawk mod is a single file. This page is the prose version.

## Roles

- **Wavesill is the brain.** Capture, FFT, dynamics, styles, colours, opacity, per-monitor settings, theme: every decision is made in Wavesill, which renders one premultiplied BGRA bitmap per taskbar every frame.
- **The mod is a dumb display.** It inserts an `Image` into each Windows 11 taskbar's XAML tree, beneath the icon layer and above the backdrop, and copies Wavesill's bitmaps into it. It interprets nothing.
- Consequence: styles can change freely without touching the mod, and the protocol, which only moves pixels, almost never changes.

## Who creates, who opens

- **The mod creates** the shared memory `Local\Wavesill.Bridge` (`CreateFileMappingW`, pagefile-backed) when it loads and closes it when it unloads. Users without the mod pay nothing.
- **Wavesill only opens** it (`OpenFileMappingW`), retrying once a second. After opening it validates `magic`, `protocol`, and that every size and offset lies inside `totalBytes`; anything else is treated as incompatible and left alone.

## Layout

```
[0, headerSize)                                    WsBridgeHeader (128 bytes)
[headerSize, headerSize + slotCount × slotSize)    WsBridgeSlot[slotCount] (144 bytes each)
then                                               two pixel buffers per slot, bufferBytes each
```

The mod fills the fixed header fields and every slot's `bufferOffset[2]` / `bufferBytes` at creation (each buffer at least 1 MB; the mod uses 4 slots × 3 MB, enough for a 5120 × 150 px taskbar). All other slot fields start at zero.

## Who writes what

| Field | Writer | When |
|---|---|---|
| `header.magic / protocol / headerSize / slotSize / slotCount / slotBytes / totalBytes / modVersion` | Mod | At creation |
| `header.modHeartbeatMs` | Mod | Every 250 ms |
| `header.flags` | Mod | Progress bits (`WS_STAGE_*`), for Wavesill's About window |
| `header.appProtocol / appVersion` | Wavesill | When it opens the mapping |
| `header.appHeartbeatMs` | Wavesill | Every frame |
| `slot.taskbarHwnd` | Wavesill | Set when it claims a slot, cleared on exit |
| `slot.x y width height stride dpi edge flags` | Wavesill | Every update; `flags` carries `WS_SLOT_PRIMARY` |
| `slot.front / frameSeq` | Wavesill | Flipped after a frame's pixels are complete (pixels, then a barrier, then `front` and `frameSeq`) |
| `slot.visible` | Wavesill | Every frame: 0 means show nothing (silence decayed to empty, paused, a full-screen app in front, taskbar hidden, monitor switched off) |
| `slot.appHeartbeatMs` | Wavesill | Every update |
| `slot.modHeartbeatMs / modStatus` | Mod | Every frame it displayed that slot (`modStatus`: 0 idle, 1 displaying, 2 taskbar not found, 3 error) |

All timestamps are `GetTickCount64()` milliseconds; both processes read the same clock.

## Heartbeats and degradation

- Wavesill hides its overlay on a taskbar only while that slot's `modHeartbeatMs` is fresh (< 500 ms) and `modStatus == 1`; otherwise it shows the overlay. If `header.modHeartbeatMs` is older than 3 s it closes the mapping and retries opening once a second.
- The mod hides its element when a slot's `appHeartbeatMs` is older than 500 ms or `visible == 0`, and hides everything when `header.appHeartbeatMs` is older than 3 s.
- Whichever side disappears (mod disabled, Windhawk uninstalled, a Windows update breaking the mod, Explorer restarting, Wavesill exiting), the worst the user sees is the other side's behaviour. Nothing is blank and nothing is drawn twice.

## Versioning

- `WS_BRIDGE_PROTOCOL` is the only number the two sides must agree on; it is independent of the app's and the mod's version numbers, which are recorded for display only.
- Wavesill accepts exactly its own protocol number; the mod displays only when `header.appProtocol` is one it supports. A mismatch means "do nothing", never "guess".
- Fields are only appended. `headerSize` / `slotSize` let a newer reader skip an older writer's shorter struct and vice versa. Reserved bytes are zero.

## How the mod pairs a taskbar with a slot

By window handle: the mod reaches a taskbar's XAML tree through its window (`Shell_TrayWnd` or `Shell_SecondaryTrayWnd`), and Wavesill records the same handle in `slot.taskbarHwnd` when it claims a slot, so each taskbar takes exactly the slot that carries its handle. (Mods before 0.7.0 compared width, height and DPI instead and used `WS_SLOT_PRIMARY` to tell mirrored taskbars apart; the flag is still written.)

## What the mod trusts

The mod lays the mapping out itself and keeps that layout in its own variables; it never reads `headerSize`, `slotSize`, `slotCount`, `bufferOffset` or `bufferBytes` back from the mapping, which any process of the same user can write to. Per frame it takes one snapshot of `width`, `height`, `stride` and `front` and copies only if `stride ≥ width × 4` and `stride × (height − 1) + width × 4` fits in the buffer, in 64-bit arithmetic.
