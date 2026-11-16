//! Actor slot insert and remove, with the matching score and display updates
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_actors_setActorDetached (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Takes battle actor slot `arg0` out of the battle when `arg1` is nonzero, otherwise puts it back in.
pub fn vs_actors_setActorDetached(arg0: c_int, arg1: c_int) callconv(.c) void {
    if (psx.at(v.g.vs_battle_actors(), arg0).*) |temp_a0| {
        if (arg1 != 0) {
            const temp_s0 = c._removeActor(@ptrCast(temp_a0));
            c.vs_actors_setActorAnimationLocked(arg0, arg1);
            if (temp_s0 != 0) {
                c.vs_battle_aiClearActorAiLinksAndPlan(arg0);
                _ = c.vs_battle_aiRebuildActorActionPlans(null);
            }
        } else {
            const temp_s0 = c._insertActor(@ptrCast(temp_a0));
            c.vs_actors_setActorAnimationLocked(arg0, 0);
            if (temp_s0 != 0) {
                c.func_800E5998();
                _ = c.vs_battle_aiRebuildActorActionPlans(null);
            }
        }
    }
}
