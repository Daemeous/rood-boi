//! Calls vs_actors_enableActorModel for each of the 16 slots (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");

/// orig: vs_actors_enableAllActorModels (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Runs vs_actors_enableActorModel for slots 0 through 15, in order.
pub fn vs_actors_enableAllActorModels() callconv(.c) void {
    var i: c_int = 0;
    while (i < 16) : (i +%= 1) {
        c.vs_actors_enableActorModel(i);
    }
}
