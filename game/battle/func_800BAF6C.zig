//! The event script's actor flag 1 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800BAF6C (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Sets (arg0 != 0) or clears bit 0 of vs_actors_actorStateFlags's flags for every live actor 1..16, then vs_actors_setOtherActorLogicEnabled(arg0).
pub fn func_800BAF6C(arg0: c_short) callconv(.c) void {
    var actor: c_short = 1;
    while (actor < 17) : (actor += 1) { // 1..16
        if (c.vs_actors_isBattleActorSlot(actor) != null) {
            if (arg0 != 0) {
                c.vs_actors_setActorDisplayFlags(actor, @bitCast(c.vs_actors_actorStateFlags(actor) | 1));
            } else {
                c.vs_actors_setActorDisplayFlags(actor, @bitCast(c.vs_actors_actorStateFlags(actor) & ~@as(c_uint, 1)));
            }
        }
    }
    _ = c.vs_actors_setOtherActorLogicEnabled(arg0);
}
