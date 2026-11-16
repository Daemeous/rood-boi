//! Recoverable HP of a hit's target: max HP minus current HP (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: _getRecoverableHp (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns how much HP the hit's target can recover: its max HP minus its current HP.
pub fn getRecoverableHp(_: ?*v.vs_action_t, _: ?*v._hitEntity_t, arg2: ?*v._hitEntity_t, _: c_int, _: c_int) callconv(.c) c_short {
    const hit = arg2.?;
    const target = v.g.vs_battle_actors()[hit.target.targetActor].?;
    const stats = psx.get(target.stats).?;
    return psx.conv(c_short, @as(c_int, stats.maxHP) -% @as(c_int, stats.currentHP));
}
