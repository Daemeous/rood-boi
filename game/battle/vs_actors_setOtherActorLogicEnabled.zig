//! Calls the slot routine vs_actors_setActorLogicEnabled for slots 1 to 15 (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");

/// orig: vs_actors_setOtherActorLogicEnabled (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Runs vs_actors_setActorLogicEnabled for each slot 1 to 15, then returns its argument.
pub fn vs_actors_setOtherActorLogicEnabled(arg0: c_int) callconv(.c) c_int {
    var i: c_int = 1;
    while (i < 16) : (i += 1) {
        _ = c.vs_actors_setActorLogicEnabled(i, arg0);
    }
    return arg0;
}
