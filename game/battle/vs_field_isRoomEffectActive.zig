//! Reads the first byte of entry `arg0` in the table vs_field_roomEffectInstances (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");

/// orig: vs_field_isRoomEffectActive (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Returns the unk0 byte of vs_field_roomEffectInstances entry `arg0`, unchecked as the C is.
pub fn vs_field_isRoomEffectActive(arg0: c_int) callconv(.c) c_int {
    return @as(c_int, psx.at(v.g.vs_field_roomEffectInstances(), arg0).unk0);
}
