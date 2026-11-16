//! Battle unit 146C: a call to vs_actors_beginActorDeath with a null second argument (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");

/// orig: func_8007C348 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Calls vs_actors_beginActorDeath with arg0, a null pointer, and arg1.
pub fn func_8007C348(arg0: c_int, arg1: c_int) callconv(.c) void {
    c.vs_actors_beginActorDeath(arg0, null, arg1);
}
