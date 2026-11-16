//! Gem modifier for one hit: the attacker's buff raises it, the defender's lowers it
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: _getHitGemModifier (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Starts at 100, adds the source's attack gem buff and subtracts the target's defense gem buff.
pub fn getHitGemModifier(action: ?*v.vs_action_t, source: ?*v._hitEntity_t, target: ?*v._hitEntity_t, hitNumber: c_int, _: c_int) callconv(.c) c_int {
    var gemModifier: c_int = 100;
    if (source) |src| {
        if (src.unk40 == 0) {
            const actor = v.g.vs_battle_actors()[src.target.targetActor].?;
            gemModifier +%= @as(c_int, c._getAttackGemBuff(@ptrCast(action), @ptrCast(psx.get(actor.stats))));
        }
    }
    if (target) |tgt| {
        if (tgt.unk40 == 0) {
            const actor = v.g.vs_battle_actors()[tgt.target.targetActor].?;
            gemModifier -%= @as(c_int, c._getDefenseGemBuff(@ptrCast(action), @ptrCast(psx.get(actor.stats)), hitNumber));
        }
    }
    return gemModifier;
}
