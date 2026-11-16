//! Works out which status changes a hit applies to its target
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");

/// orig: _setApplicableStatuses (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Adds the status changes of effect arg0 that the target can take to the hit's remove and inflict masks, and returns both.
pub fn setApplicableStatuses(arg0: c_int, entity: ?*v._hitEntity_t) callconv(.c) c_int {
    var inflict: c_int = 0;
    var remove: c_int = 0;
    const params = psx.at(v.g.vs_main_statusEffectParams(), arg0);
    const e = entity.?;
    const temp_a2 = psx.get(v.g.vs_battle_actors()[e.target.targetActor].?.stats).?;

    if (temp_a2.actorId != 128) {
        const temp_t0: c_int = temp_a2.statuses;

        if ((temp_t0 & params[3]) == 0) {
            remove = params[0] & ~temp_a2.statusImmunities & (~temp_t0 | params[2]);
            e.removeStatuses |= remove;
            inflict = params[2] & temp_a2.statuses;
            e.inflictStatuses |= inflict;
        }
    }

    return remove | inflict;
}
