//! Stores two halfword globals of the battle overlay (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: vs_field_setBackgroundParallaxScale (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Stores the two arguments into vs_field_backgroundParallaxScaleXFixed12 and vs_field_backgroundParallaxScaleYFixed12, truncated to halfwords.
pub fn vs_field_setBackgroundParallaxScale(arg0: c_int, arg1: c_int) callconv(.c) void {
    v.g.vs_field_backgroundParallaxScaleXFixed12().* = psx.conv(i16, arg0);
    v.g.vs_field_backgroundParallaxScaleYFixed12().* = psx.conv(i16, arg1);
}
