//! Recoverable MP of a hit's target: its max MP minus its current MP (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: _getRecoverableMp (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// The MP the target of a hit can recover: its max MP minus its current MP.
pub fn getRecoverableMp(_: ?*v.vs_action_t, _: ?*v._hitEntity_t, arg2: ?*v._hitEntity_t, _: c_int, _: c_int) callconv(.c) c_short {
    const target: usize = arg2.?.target.targetActor;
    const stats = psx.get(v.g.vs_battle_actors()[target].?.stats).?;
    const maxMP: c_int = stats.maxMP;
    const currentMP: c_int = stats.currentMP;
    return psx.conv(c_short, maxMP -% currentMP);
}
