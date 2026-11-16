//! Stores a one-byte battle flag (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");

/// orig: vs_field_setMovingPlatformsPaused (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Stores the low byte of its argument in vs_field_areMovingPlatformsPaused.
pub fn vs_field_setMovingPlatformsPaused(arg0: c_int) callconv(.c) void {
    v.g.vs_field_areMovingPlatformsPaused().* = psx.conv(i8, arg0);
}
