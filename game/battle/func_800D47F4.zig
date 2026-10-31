//! Runs the effect executor selected by the first call's byte, with a 32-bit argument built from two halfwords or the override at unkD1C.unk3C (src/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D47F4 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Runs the effect executor picked from D_800EC324 by the byte func_800D5170 returns, with the argument built from two halfwords (or the override in unkD1C.unk3C).
pub fn func_800D47F4(arg0: ?*v.D_800F53B8_t) callconv(.c) c_int {
    const temp_s2: c_int = @as(c_int, c.func_800D5170(@ptrCast(arg0)));
    var temp_s0: c_int = @as(c_int, c.func_800D5198(@ptrCast(arg0)));
    const hi: c_uint = @as(c_uint, c.func_800D5198(@ptrCast(arg0))) << 16;
    temp_s0 = temp_s0 +% @as(c_int, @bitCast(hi));

    if (arg0.?.unkD1C.unk3C != 0) {
        temp_s0 = arg0.?.unkD1C.unk3C;
    }

    const effects = psx.globalMany(c.effectExec, "D_800EC324");
    _ = c.func_800CF694(@ptrCast(arg0), effects[@intCast(temp_s2)], temp_s0);
    return 1;
}
