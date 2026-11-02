//! Linear interpolation between two VECTORs by step / steps (src/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D1884 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// func_800D17A8 (the per-axis lerp) from `from` to `to` by step * 8192 / steps into `out`.
pub fn func_800D1884(from: ?*v.VECTOR, to: ?*v.VECTOR, steps: c_int, step: c_int, out: ?*v.VECTOR) callconv(.c) void {
    c.func_800D17A8(@ptrCast(from), @ptrCast(to), @divTrunc(step *% (4096 * 2), steps), @ptrCast(out));
}
