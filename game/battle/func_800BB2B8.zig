//! Screen effect start driven by a short argument in the script (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800BB2B8 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Starts the screen effect whose short argument follows the opcode byte at `arg0`.
pub fn func_800BB2B8(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    c.pc_screff2_start(@as(c_int, c.vs_battle_getShort(arg0 + 1)));
    return 0;
}
