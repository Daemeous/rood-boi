//! Sets bit 2 of each of the 16 slot flags that lack it (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");

/// orig: vs_actors_freezeAllActorAnimations (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sets flag bit 4 on each of the 16 slots whose flags lack it.
pub fn vs_actors_freezeAllActorAnimations() callconv(.c) void {
    var i: c_int = 0;
    while (i < 16) : (i +%= 1) {
        const temp_v0: c_uint = c.vs_actors_actorStateFlags(i);
        if ((temp_v0 & 4) == 0) {
            c.vs_actors_setActorDisplayFlags(i, @bitCast(temp_v0 | 4));
        }
    }
}
