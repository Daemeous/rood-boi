//! Script opcode that frees a sound effect and reports whether the second
//! result flag is set (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_battle_script_freeSfx (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Frees the sound effect named by the script's operand byte; returns 4 when the global flag is 2, else 0.
pub fn script_freeSfx(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    _ = c.vs_main_freeSfx(psx.conv(c_uint, arg0[1]));
    return @as(c_int, @intFromBool(v.g.D_800F4C2C().* == 2)) *% 4;
}
