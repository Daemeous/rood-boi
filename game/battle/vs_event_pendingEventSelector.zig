//! Packs the battle event file index with vs_event_eventSource (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");

/// orig: vs_event_pendingEventSelector (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Returns vs_event_eventSource shifted up 12 bits, OR'd with the event file index, as a short.
pub fn vs_event_pendingEventSelector() callconv(.c) c_short {
    // u16 promotes to int before the shift; the result fits in 32 bits, so the plain shift is exact.
    const hi: c_int = @as(c_int, v.g.vs_event_eventSource().*) << 12;
    const lo: c_int = @as(c_int, v.g._evtFile().*);
    return psx.conv(c_short, hi | lo);
}
