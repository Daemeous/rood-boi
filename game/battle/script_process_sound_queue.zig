//! Sound queue step for the script interpreter (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: vs_battle_script_processSoundQueue (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Runs one step of the sound queue and reports whether it returned 1 (done).
pub fn script_processSoundQueue(_: [*c]u8, _: c_short) callconv(.c) c_int {
    return @intFromBool(c.vs_main_processSoundQueue() == 1);
}
