//! Battle script opcode: frees the music named by the script's operand (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_battle_script_freeMusic (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Frees the music track the script names, then returns 4 if the music state is 2, else 0.
pub fn script_freeMusic(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    _ = c.vs_main_freeMusic(@as(c_int, arg0[1]));
    return @as(c_int, @intFromBool(v.g.D_800F4C2C().* == 2)) *% 4;
}
