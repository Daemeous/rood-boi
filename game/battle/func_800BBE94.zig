//! Sets the view vector at unk1EC from the vector func_800A1108 looks up for the
//! index in unk1E8 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BBE94 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Zeroes the view vector for index 0x2000, else sets it from the looked-up vector in 4.12 fixed point.
pub fn func_800BBE94() callconv(.c) void {
    var sp10: v.func_8006EBF8_t = undefined;

    if (v.g.D_800F4BA4().*.?.unk1E8 == 0x2000) {
        v.g.D_800F4BA4().*.?.unk1EC.vz = 0;
        v.g.D_800F4BA4().*.?.unk1EC.vy = 0;
        v.g.D_800F4BA4().*.?.unk1EC.vx = 0;
        return;
    }
    _ = c.func_800A1108(@as(c_int, v.g.D_800F4BA4().*.?.unk1E8), &sp10);
    v.g.D_800F4BA4().*.?.unk1EC.vx = @as(c_int, sp10.unk0.unk4.vx) *% 4096;
    v.g.D_800F4BA4().*.?.unk1EC.vy = @as(c_int, sp10.unk0.unk4.vy) *% 4096;
    v.g.D_800F4BA4().*.?.unk1EC.vz = @as(c_int, sp10.unk0.unk4.vz) *% 4096;
}
