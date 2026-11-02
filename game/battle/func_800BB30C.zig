//! Battle helper: calls func_800CB7DC, then reports whether D_800F4C2C is 2 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BB30C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_800CB7DC, then returns 4 when D_800F4C2C is 2, else 0.
pub fn func_800BB30C(_: [*c]u8, _: c_short) callconv(.c) c_int {
    c.func_800CB7DC();
    return if (v.g.D_800F4C2C().* == 2) 4 else 0;
}
