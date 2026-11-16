//! Chain evasion of the targeted limb, as a chain hit rate modifier (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: _getChainEvasionModifier (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// The chain hit rate modifier: 255 minus the target limb's chain evasion, scaled to percent, never 255.
pub fn getChainEvasionModifier(_: ?*v.vs_action_t, _: ?*v._hitEntity_t, target: ?*v._hitEntity_t, _: c_int, _: c_int) callconv(.c) c_int {
    const t = target.?;
    const actor = v.g.vs_battle_actors()[t.target.targetActor].?;
    const stats = psx.get(actor.stats).?;
    const limb = psx.at(&stats.limbs, t.target.targetLimb);
    var rate: c_int = @divTrunc((255 -% @as(c_int, limb.chainEvasion)) *% 100, 255);
    if (rate == 255) {
        rate = 254;
    }
    return rate;
}
