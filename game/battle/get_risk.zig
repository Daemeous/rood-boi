//! Risk of the enemy a hit entity targets (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: _getRisk (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns the risk of the battle actor that the hit entity `target` aims at.
pub fn getRisk(_: ?*v.vs_action_t, _: ?*v._hitEntity_t, target: ?*v._hitEntity_t, _: c_int, _: c_int) callconv(.c) c_short {
    // vs_battle_actors has no bounds check in the C: psx.at indexes it as C does.
    const actor = psx.at(v.g.vs_battle_actors(), target.?.target.targetActor).*.?;
    const stats = psx.get(actor.stats).?;
    return psx.conv(c_short, stats.risk);
}
