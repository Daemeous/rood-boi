//! Battle unit 146C: stores a 16-bit value in vs_field_areRoomUpdatesPaused (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: vs_field_setRoomAnimationsPaused (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Stores its argument, truncated to 16 bits, in vs_field_areRoomUpdatesPaused.
pub fn vs_field_setRoomAnimationsPaused(arg0: c_int) callconv(.c) void {
    v.g.vs_field_areRoomUpdatesPaused().* = psx.conv(u16, arg0);
}
