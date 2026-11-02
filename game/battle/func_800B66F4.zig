//! Battle check on the global pointer D_800F4BBC (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");

/// orig: func_800B66F4 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Returns 1 when D_800F4BBC is set and the short it points at is not 4, else 0.
pub fn func_800B66F4(_: [*c]u8, _: c_short) callconv(.c) c_int {
    const p = v.g.D_800F4BBC().*;
    if (p != null and @as(c_int, p.?[0]) != 4) return 1;
    return 0;
}
