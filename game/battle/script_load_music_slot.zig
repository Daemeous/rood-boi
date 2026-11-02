//! Script opcode: loads a music slot named by the script's operands (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: vs_battle_script_loadMusicSlot (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Loads the music slot named by operands 1 and 2 of the script; returns 0.
pub fn script_loadMusicSlot(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    _ = c.vs_main_loadMusicSlot(@as(c_int, arg0[1]), @as(c_int, arg0[2]));
    return 0;
}
