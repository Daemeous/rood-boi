//! Risk modifier of a hit against its target, in percent of the base rate
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: _getRiskModifier (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Scales the target's risk stat into a hit rate, capped at 254.
pub fn getRiskModifier(_: ?*v.vs_action_t, source: ?*v._hitEntity_t, _: ?*v._hitEntity_t, _: c_int, _: c_int) callconv(.c) c_int {
    // The target index is a u8 and the table is many-item: no bounds check, as in the C.
    const actor = v.g.vs_battle_actors()[source.?.target.targetActor].?;
    const stats = psx.get(actor.stats).?;
    var rate: c_int = @divTrunc((@as(c_int, stats.risk) +% 150) *% 100, 256);
    if (rate == 255) {
        rate = 254;
    }
    return rate;
}
