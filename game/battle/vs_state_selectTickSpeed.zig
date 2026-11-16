//! Game-time tick speed: sets it to 2 or 4 when asked and returns it (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");

/// orig: vs_state_selectTickSpeed (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Stores `arg0` as the tick speed when it is 2 or 4, and returns the tick speed.
pub fn vs_state_selectTickSpeed(arg0: c_int) callconv(.c) c_int {
    if (arg0 == 2 or arg0 == 4) {
        v.g.vs_gametime_tickspeed().* = arg0;
    }
    return v.g.vs_gametime_tickspeed().*;
}
