// =====================================================================================
//  Wavesill bridge — shared-memory protocol between Wavesill.exe and an optional
//  Windhawk mod that displays Wavesill's frames *inside* the Windows 11 taskbar
//  (beneath the icons, above the backdrop).
//
//  This header is the whole contract.  It is plain C, has no dependencies, and is
//  meant to be included verbatim by both sides.  See BRIDGE.md for the prose version.
//
//  Roles
//  -----
//  * The MOD owns the mapping.  It creates WS_BRIDGE_MAPPING_NAME when it loads and
//    closes it when it unloads.  Standard users (no mod) never pay for it.
//  * The APP opens the mapping (never creates it), validates the header, then for each
//    taskbar it renders it claims a slot and writes finished, premultiplied BGRA frames
//    into that slot's double buffer.
//  * The MOD blits `slot.buffer[slot.front]` into a visual it inserted into that
//    taskbar's XAML tree.  The mod does NOT interpret audio, styles or colours; the app
//    renders everything.  That is why a mod almost never has to change.
//
//  Heartbeats (all GetTickCount64() milliseconds, same clock in both processes)
//  ---------------------------------------------------------------------------
//  * header.modHeartbeatMs   — mod writes every frame while loaded and healthy.
//  * header.appHeartbeatMs   — app writes every frame while connected.
//  * slot.modHeartbeatMs     — mod writes every frame it actually displayed that slot.
//  * slot.appHeartbeatMs     — app writes every time it updates that slot.
//  The app hides its own overlay for a taskbar only while slot.modHeartbeatMs is fresh
//  (< WS_BRIDGE_FRESH_MS).  The mod hides its visual while slot.appHeartbeatMs is stale
//  or slot.visible == 0.  Either side vanishing therefore degrades cleanly to the other
//  side's behaviour within a fraction of a second.  Nothing is ever half-drawn.
//
//  Versioning
//  ----------
//  * WS_BRIDGE_PROTOCOL is the only number that matters between the two sides.  It is
//    independent of the app's and the mod's own version numbers (which are recorded in
//    the header purely for display).
//  * A mod advertises the protocol it implements in header.protocol.  An app that does
//    not implement that exact protocol leaves the mapping alone (keeps its overlay); a
//    mod that finds header.appProtocol unsupported keeps its visual hidden.  Mismatch
//    means "do nothing", never "guess".
//  * Fields are only ever appended.  headerSize / slotSize let a newer reader skip an
//    older writer's shorter structs and vice versa.  Reserved bytes are zero.
//
//  Memory ordering
//  ---------------
//  Writers finish all field/pixel writes, then store `front`/`frameSeq` (app) or the
//  heartbeat (both) with a release barrier (InterlockedExchange / MemoryBarrier).
//  Readers load those with acquire semantics before touching the data they guard.
// =====================================================================================
#ifndef WAVESILL_BRIDGE_H
#define WAVESILL_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WS_BRIDGE_MAPPING_NAME  L"Local\\Wavesill.Bridge"
#define WS_BRIDGE_MAGIC         0x4C495357u          /* "WSIL" little-endian */
#define WS_BRIDGE_PROTOCOL      1u
#define WS_BRIDGE_FRESH_MS      500u                 /* heartbeat considered alive */
#define WS_BRIDGE_MAX_SLOTS     8u                   /* hard cap; mod picks 1..8 */
#define WS_BRIDGE_MIN_SLOT_BYTES (2u * 1024u * 1024u) /* mod must give each slot >= 2 MB */

/* Pixel format of every frame: 32-bit BGRA, alpha premultiplied, top-down rows,
   `stride` bytes per row.  Identical to DXGI_FORMAT_B8G8R8A8_UNORM with
   D2D1_ALPHA_MODE_PREMULTIPLIED, and to what UpdateLayeredWindow consumes. */

/* Taskbar edge, same values as ABE_* in shellapi.h */
enum { WS_EDGE_LEFT = 0, WS_EDGE_TOP = 1, WS_EDGE_RIGHT = 2, WS_EDGE_BOTTOM = 3 };

#pragma pack(push, 8)

typedef struct WsBridgeHeader {
    uint32_t magic;          /* WS_BRIDGE_MAGIC                                        */
    uint32_t protocol;       /* mod: WS_BRIDGE_PROTOCOL it implements                  */
    uint32_t headerSize;     /* mod: sizeof(WsBridgeHeader) it was built with          */
    uint32_t slotSize;       /* mod: sizeof(WsBridgeSlot) it was built with            */
    uint32_t slotCount;      /* mod: number of slots that follow the header (1..8)     */
    uint32_t slotBytes;      /* mod: pixel bytes reserved per slot (two buffers share) */
    uint32_t totalBytes;     /* mod: size of the whole mapping                         */
    uint32_t modVersion;     /* mod: (major<<16)|(minor<<8)|patch, display only        */
    uint64_t modHeartbeatMs; /* mod: GetTickCount64() every frame                      */
    uint32_t appProtocol;    /* app: WS_BRIDGE_PROTOCOL it implements                  */
    uint32_t appVersion;     /* app: (major<<16)|(minor<<8)|patch, display only        */
    uint64_t appHeartbeatMs; /* app: GetTickCount64() every frame while connected      */
    uint32_t flags;          /* mod: WS_STAGE_* progress bits (diagnostics for About)  */
    uint8_t  reserved[68];   /* zero; future fields are appended here                  */
} WsBridgeHeader;            /* 128 bytes */

typedef struct WsBridgeSlot {
    /* ---- written by the app when it claims / updates the slot ---- */
    uint64_t taskbarHwnd;    /* HWND of Shell_TrayWnd / Shell_SecondaryTrayWnd; 0 = free */
    int32_t  x, y;           /* taskbar rect, physical screen pixels                   */
    int32_t  width, height;  /* frame size == taskbar size                             */
    uint32_t stride;         /* bytes per row (== width * 4)                           */
    uint32_t dpi;            /* taskbar DPI                                            */
    uint32_t edge;           /* WS_EDGE_*                                              */
    uint32_t bufferOffset[2];/* byte offsets of the two pixel buffers, from mapping base */
    uint32_t bufferBytes;    /* capacity of each buffer                                 */
    uint32_t front;          /* index (0/1) of the buffer holding the latest frame     */
    uint32_t frameSeq;       /* increments after every completed frame                 */
    uint32_t visible;        /* 1 = show it; 0 = app wants nothing shown (silence,     */
                             /*     paused, fullscreen app in front, taskbar hidden)   */
    uint64_t appHeartbeatMs; /* app: GetTickCount64() every slot update                */
    uint32_t flags;          /* app: WS_SLOT_PRIMARY when this is the primary taskbar  */
    /* ---- written by the mod ---- */
    uint64_t modHeartbeatMs; /* mod: every frame it displayed this slot                */
    uint32_t modStatus;      /* mod: 0 idle, 1 displaying, 2 taskbar not found, 3 error */
    uint8_t  reserved[52];   /* zero                                                   */
} WsBridgeSlot;              /* 144 bytes */

enum { WS_SLOT_PRIMARY = 1u };

/* header.flags: how far the mod got.  Purely informational; the app shows the highest bit
   reached in its About window so a stuck mod can be diagnosed without Windhawk's log. */
enum {
    WS_STAGE_XAML = 1u,        /* Taskbar.View.dll + Windows.UI.Xaml.dll present   */
    WS_STAGE_DIAG = 2u,        /* InitializeXamlDiagnosticsEx succeeded            */
    WS_STAGE_ADVISED = 4u,     /* AdviseVisualTreeChange succeeded                 */
    WS_STAGE_BACKGROUND = 8u,  /* Taskbar.TaskbarBackground element seen           */
    WS_STAGE_INSERTED = 16u,   /* the mod's Image is in the tree                   */
    WS_STAGE_TIMER = 32u,      /* DispatcherTimer created                          */
    WS_STAGE_TICKING = 64u,    /* first tick ran (timer or Rendering fallback)     */
    WS_STAGE_MATCHED = 128u,   /* a taskbar was paired with a slot                 */
    WS_STAGE_BLITTED = 256u,   /* at least one frame copied into the taskbar       */
    WS_STAGE_COUNT = 9u,
    WS_STAGE_OFF = 0x4000u,    /* attach disabled in the mod's settings            */
    WS_STAGE_GUARD = 0x8000u   /* attach skipped because the previous run crashed  */
};

#ifdef __cplusplus
static_assert(sizeof(WsBridgeHeader) == 128, "WsBridgeHeader must stay 128 bytes");
static_assert(sizeof(WsBridgeSlot) == 144, "WsBridgeSlot must stay 144 bytes");
#endif

/* How the mod matches a slot to a taskbar it lives in: the XAML island of a taskbar
   has the same pixel size and DPI as the taskbar window, so it compares
   (width, height, dpi); when two taskbars are identical (mirrored monitors), the slot
   with WS_SLOT_PRIMARY goes to the taskbar whose XAML tree was created first. */

#pragma pack(pop)

/* Layout of the mapping:
     [0, headerSize)                       WsBridgeHeader
     [headerSize, headerSize+slotCount*slotSize)   WsBridgeSlot[slotCount]
     then, per slot, two pixel buffers of `bufferBytes` each at bufferOffset[0/1]
   The mod fills bufferOffset/bufferBytes for every slot at creation (they are part of
   the fixed layout, the app never moves them).  All other slot fields start at zero. */

#ifdef __cplusplus
}
#endif
#endif /* WAVESILL_BRIDGE_H */
