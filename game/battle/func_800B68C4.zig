//! Takes the pending-action flag D_800F4C2C and, when it was 2, runs func_800B6778 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B68C4 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Clears D_800F4C2C; when it was 2, runs func_800B6778 on arg0 and returns 4, else returns 0.
pub fn func_800B68C4(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const flag = v.g.D_800F4C2C();
    const temp_v1: u8 = flag.*;
    flag.* = 0;
    if (temp_v1 == 2) {
        c.func_800B6778(arg0);
        return 4;
    }
    return 0;
}
