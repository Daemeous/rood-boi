//! Inflicted status effects of a hit: the statuses of parameter set `arg0` that the target does not resist, added to the hit (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: _setInflictedStatuses (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Adds the status effects of parameter set `arg0` that the target does not resist to the hit, and returns them.
pub fn setInflictedStatuses(arg0: c_int, entity: ?*v._hitEntity_t) callconv(.c) c_int {
    var ret: c_int = 0;
    // vs_main_statusEffectParams[arg0] decays to its first int; the address only, no read yet.
    const params: [*]i32 = @ptrCast(psx.at(v.g.vs_main_statusEffectParams(), arg0));
    // vs_battle_actors[targetActor] is not bounds-checked by the C (a u8 index).
    const target = psx.at(v.g.vs_battle_actors(), entity.?.target.targetActor).*.?;
    const stats = psx.get(target.stats).?;
    if (stats.actorId != 128) {
        ret = params[0] & stats.statuses;
        entity.?.inflictStatuses |= ret;
    }
    return ret;
}
