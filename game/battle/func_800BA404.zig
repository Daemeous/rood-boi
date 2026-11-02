//! Calls the 0x80045D64 helper with a table entry and a byte argument from a script record (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BA404 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Passes D_800E9B34[arg0[1]] and arg0[2] to func_80045D64; returns 0.
pub fn func_800BA404(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const table = v.g.D_800E9B34();
    c.func_80045D64(table[arg0[1]], @as(c_int, arg0[2]));
    return 0;
}
