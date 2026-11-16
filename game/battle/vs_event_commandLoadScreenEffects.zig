//! Battle overlay unit pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c: a stub that calls func_800CB79C.
const c = @import("c_4A0A8");

/// orig: vs_event_commandLoadScreenEffects (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_800CB79C and returns 0; both arguments are unused.
pub fn vs_event_commandLoadScreenEffects(_: [*c]u8, _: c_short) callconv(.c) c_int {
    c.func_800CB79C();
    return 0;
}
