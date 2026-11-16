//! Hit factor from the target's current HP and the action's stat factor (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: _factorCurrentHp (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Scales the target's current HP by the action's stat factor and adds the battle's bonus term.
pub fn factorCurrentHp(action: ?*v.vs_action_t, _: ?*v._hitEntity_t, target: ?*v._hitEntity_t, hitIndex: c_int, _: c_int) callconv(.c) c_short {
    const hitAction = action.?;
    const actor = v.g.vs_battle_actors()[target.?.target.targetActor].?;
    const stats = psx.get(actor.stats).?;
    const hp: c_int = stats.currentHP;
    const statFactor: c_int = psx.getBits(psx.at(&hitAction.hitParams, hitIndex), "statFactor");
    const product: c_int = hp *% statFactor;
    const bonus: c_uint = v.g.vs_battle_actionState().*.?.unk0 -% 1;
    const sum: c_uint = psx.conv(c_uint, @divTrunc(product, 100)) +% bonus;
    return psx.conv(i16, sum);
}
