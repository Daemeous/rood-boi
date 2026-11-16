//! Clears bit 2 of each of the 16 slot flags that vs_actors_actorStateFlags reports (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const c = @import("c_146C");

/// orig: vs_actors_unfreezeAllActorAnimations (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// For each slot 0..15 with flag bit 4 set, writes the slot back with that bit cleared.
pub fn vs_actors_unfreezeAllActorAnimations() callconv(.c) void {
    var i: c_int = 0;
    while (i < 16) : (i += 1) {
        const temp_v0: c_int = psx.conv(c_int, c.vs_actors_actorStateFlags(i));
        if ((temp_v0 & 4) != 0) {
            c.vs_actors_setActorDisplayFlags(i, temp_v0 & ~@as(c_int, 4));
        }
    }
}
