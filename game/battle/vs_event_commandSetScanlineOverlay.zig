//! Stores byte 1 of its buffer argument in vs_event_isScanlineOverlayEnabled (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");

/// orig: vs_event_commandSetScanlineOverlay (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Copies byte 1 of `arg0` into vs_event_isScanlineOverlayEnabled and returns 0; the second argument is unused.
pub fn vs_event_commandSetScanlineOverlay(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    v.g.vs_event_isScanlineOverlayEnabled().* = arg0[1];
    return 0;
}
