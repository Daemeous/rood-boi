//! Runs func_8007CEA0 for each occupied battle actor slot (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_actors_returnAllActorsToIdle (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Calls func_8007CEA0 with the index of every non-null battle actor slot.
pub fn vs_actors_returnAllActorsToIdle() callconv(.c) void {
    const actors = v.g.vs_battle_actors();
    var i: c_int = 0;
    while (i < 16) : (i +%= 1) {
        if (actors[@intCast(i)] != null) {
            c.func_8007CEA0(i);
        }
    }
}
