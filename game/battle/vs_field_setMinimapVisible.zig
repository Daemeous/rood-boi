//! Stores a byte in vs_field_isMinimapVisible and returns it (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: vs_field_setMinimapVisible (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sets the byte at vs_field_isMinimapVisible to arg0 and returns the stored value.
pub fn vs_field_setMinimapVisible(arg0: u8) callconv(.c) c_int {
    const stored = v.g.vs_field_isMinimapVisible();
    stored.* = arg0;
    return @as(c_int, stored.*);
}
