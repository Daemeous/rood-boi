//! Stores two 16-bit battle globals (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");

/// orig: vs_field_setRoomBackgroundOffset (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Stores arg0 and arg1 into the 16-bit globals vs_field_backgroundOffsetX and vs_field_backgroundOffsetY.
pub fn vs_field_setRoomBackgroundOffset(arg0: c_int, arg1: c_int) callconv(.c) void {
    v.g.vs_field_backgroundOffsetX().* = psx.conv(i16, arg0);
    v.g.vs_field_backgroundOffsetY().* = psx.conv(i16, arg1);
}
