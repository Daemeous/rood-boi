//! Passes byte 1 of its first argument to vs_state_selectTickSpeed (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const c = @import("c_4A0A8");

/// orig: vs_event_commandSetTickSpeed (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls vs_state_selectTickSpeed with the byte after the first byte of `arg0`; returns 0.
pub fn vs_event_commandSetTickSpeed(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    _ = c.vs_state_selectTickSpeed(@as(c_int, psx.byteAt(u8, arg0, 1).*));
    return 0;
}
