//! Battle helper: calls func_800CB7DC, then reports whether vs_event_skipState is 2 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_commandFreeScreenEffects (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_800CB7DC, then returns 4 when vs_event_skipState is 2, else 0.
pub fn vs_event_commandFreeScreenEffects(_: [*c]u8, _: c_short) callconv(.c) c_int {
    c.func_800CB7DC();
    return if (v.g.vs_event_skipState().* == 2) 4 else 0;
}
