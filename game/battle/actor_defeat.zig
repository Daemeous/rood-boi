//! An actor's defeat (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_beginActorDefeatAnimation (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Settles actor `actorId` after a hit: at 0 HP it falls; 1 when it is defeated (not a 0x80 actor, or Ashley).
pub fn checkDefeated(actorId: c_int) callconv(.c) c_int {
    c.func_800E6700(actorId);
    var ret: c_int = 0;

    // vs_battle_actors[actorId]: the C does not check the id, and reads the slot again after each call.
    const actors = v.g.vs_battle_actors();
    if (psx.at(actors, actorId).* != null) {
        psx.at(actors, actorId).*.?.unk8 = 0;
        if (psx.get(psx.at(actors, actorId).*.?.stats).?.currentHP == 0) {
            c.vs_battle_setActorMarker(actorId, 0);
            psx.at(actors, actorId).*.?.unk8 = 0x20;
            if (psx.at(actors, actorId).*.?.unk26 == 0) {
                c.vs_actors_beginActorDeath(actorId, &v.g.vs_battle_actionState().*.?.rangeOrigin, 1);
                c.vs_actors_setActorAnimationLocked(actorId, 1);
                if (psx.at(actors, actorId).*.?.unk27 != 0x80 or actorId == 0) {
                    ret = 1;
                }
            }
        }
    }
    return ret;
}
