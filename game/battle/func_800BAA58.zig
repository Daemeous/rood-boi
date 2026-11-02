//! Battle unit 4A0A8: a two-state wait step (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");
const v = @import("views_4A0A8");

/// orig: func_800BAA58 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Runs func_800CF1A8 and sets D_800F4BB9 on the first call; afterwards clears it and returns 0 when func_8007BEE4 reads 0 or 5, else returns 1.
pub fn func_800BAA58(_: [*c]u8, _: c_short) callconv(.c) c_int {
    if (v.g.D_800F4BB9().* == 0) {
        c.func_800CF1A8();
        v.g.D_800F4BB9().* = 1;
    } else if (c.func_8007BEE4() == 0 or c.func_8007BEE4() == 5) {
        v.g.D_800F4BB9().* = 0;
        return 0;
    }
    return 1;
}
