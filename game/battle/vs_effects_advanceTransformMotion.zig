//! Per-frame step of an animated curve: runs the mode its flags select, then advances the frame counter up to the limit (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_advanceTransformMotion (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Runs the step function selected by arg0's flags, then advances arg1's frame while it is below arg0's limit.
pub fn vs_effects_advanceTransformMotion(arg0: ?*v.func_800D0548_arg0, arg1: ?*v.func_800D0548_arg1) callconv(.c) void {
    const a = arg0.?;
    const b = arg1.?;

    switch (@as(c_int, a.flags) & 0x3000) {
        0 => {},
        0x1000 => c.vs_effects_interpolateTransformPosition(psx.cptr(c.func_800D0548_arg0, a), psx.cptr(c.func_800D0548_arg1, b)),
        0x2000 => c.vs_effects_interpolateTransformSpline(psx.cptr(c.func_800D0548_arg0, a), psx.cptr(c.func_800D0548_arg1, b)),
        else => {},
    }

    if ((@as(c_int, b.flags) & 4) != 0) {
        if (@as(c_int, a.limit) >= @as(c_int, b.frame)) {
            b.frame +%= 1;
        }
    }
}
