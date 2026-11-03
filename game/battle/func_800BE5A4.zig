//! Camera look-at and eye tween steps (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BE5A4 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Steps the camera look-at point (arg0 0) or eye (other nonzero); arg0 2 steps both and averages their durations.
pub fn func_800BE5A4(arg0: c_short) callconv(.c) void {
    const base = v.g.D_800F4BA4().*.?;
    const temp_s0: *v.D_800F4BA4_t2 = &base.lookAt;
    const temp_s1: *v.D_800F4BA4_t2 = &base.eye;

    if (arg0 == 2) {
        c.func_800BE53C(psx.cptr(c.D_800F4BA4_t2, temp_s0));
        c.func_800BE53C(psx.cptr(c.D_800F4BA4_t2, temp_s1));
        // two u8 durations: the sum is at most 510.
        const averaged: u8 = psx.conv(u8, (@as(c_int, temp_s0.duration) + @as(c_int, temp_s1.duration)) >> 1);
        temp_s0.duration = averaged;
        temp_s1.duration = averaged;
        return;
    }

    const var_a0: *v.D_800F4BA4_t2 = if (arg0 != 0) temp_s1 else temp_s0;
    c.func_800BE53C(psx.cptr(c.D_800F4BA4_t2, var_a0));
}
