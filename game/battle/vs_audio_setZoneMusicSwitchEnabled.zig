//! Stores a value in the battle global vs_audio_zoneMusicSwitchEnabled (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: vs_audio_setZoneMusicSwitchEnabled (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Stores its argument in vs_audio_zoneMusicSwitchEnabled and returns it.
pub fn vs_audio_setZoneMusicSwitchEnabled(arg0: c_int) callconv(.c) c_int {
    v.g._zoneContext().unk30 = arg0;
    return arg0;
}
