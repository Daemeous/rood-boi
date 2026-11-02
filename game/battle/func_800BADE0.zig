//! Battle script step: starts a timed wait on the first call and reports done once it expires (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BADE0 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Starts the wait described by the script bytes on the first call; afterwards returns 1 while it runs and 4 when it has finished.
pub fn func_800BADE0(arg0: [*c]u8) callconv(.c) c_int {
    if (v.g.D_800F4BF8().* == 0) {
        // GCC evaluates the call arguments right to left: the getShort at arg0 + 4 runs before the one at arg0 + 1.
        const fourth: c_int = @as(c_int, arg0[6]);
        const third: c_int = @as(c_int, c.vs_battle_getShort(arg0 + 4));
        const second: c_int = @as(c_int, arg0[3]);
        const first: c_int = @as(c_int, c.vs_battle_getShort(arg0 + 1));
        c.func_8007BA98(first, second, third, fourth);
        v.g.D_800F4BF8().* = 1;
        return 1;
    }

    if (c.func_8007B9FC() == 0) {
        v.g.D_800F4BF8().* = 0;
        return 4;
    }
    return 1;
}
