//! A one-byte store into a battle global (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: vs_field_setTiledBackgroundEnabled (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Stores its argument, truncated to a byte, in vs_field_isTiledBackgroundEnabled.
pub fn vs_field_setTiledBackgroundEnabled(value: c_int) callconv(.c) void {
    v.g.vs_field_isTiledBackgroundEnabled().* = psx.conv(u8, value);
}
