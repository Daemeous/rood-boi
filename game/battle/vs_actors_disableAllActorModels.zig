//! Calls the per-slot helper for slots 0 to 15 (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");

/// orig: vs_actors_disableAllActorModels (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Runs vs_actors_disableActorModel for each of the 16 slots, in order.
pub fn vs_actors_disableAllActorModels() callconv(.c) void {
    var i: c_int = 0;
    while (i < 16) : (i +%= 1) {
        c.vs_actors_disableActorModel(i);
    }
}
