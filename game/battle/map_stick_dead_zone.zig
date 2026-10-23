//! Stick dead-zone mapping for the battle camera (src/BATTLE/BATTLE.PRG/5BE50.c).

/// orig: vs_battle_mapStickDeadZone (src/BATTLE/BATTLE.PRG/5BE50.c)
/// Maps a raw stick axis to its offset past the dead zone [64, 192); inside the zone the result is 0.
pub fn mapStickDeadZone(arg0: c_int) callconv(.c) c_int {
    if (arg0 < 64) {
        return arg0 -% 64;
    }
    if (arg0 >= 192) {
        return arg0 -% 192;
    }
    return 0;
}
