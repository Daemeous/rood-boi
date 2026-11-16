//! Forwards to vs_actors_setActorYawTurn (pc/src/game/BATTLE/BATTLE.PRG/40564.c).
const c = @import("c_40564");

/// orig: vs_actors_setActorYawMotion (pc/src/game/BATTLE/BATTLE.PRG/40564.c)
/// Passes its three arguments straight on to vs_actors_setActorYawTurn.
pub fn vs_actors_setActorYawMotion(arg0: c_int, arg1: c_short, arg2: c_int) callconv(.c) void {
    c.vs_actors_setActorYawTurn(arg0, arg1, arg2);
}
