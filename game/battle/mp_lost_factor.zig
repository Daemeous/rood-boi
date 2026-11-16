//! Magic point loss factor for a hit: the target's lost MP scaled by the hit's stat factor, plus the battle level term (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: _mpLostFactor (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Scales the target's lost MP by hit `hitIndex`'s stat factor and adds the level term.
pub fn mpLostFactor(action: ?*v.vs_action_t, target: ?*v._hitEntity_t, _: ?*v._hitEntity_t, hitIndex: c_int, _: c_int) callconv(.c) c_short {
    // The C does not check targetActor (a u8) against the slot count.
    const slot = psx.at(v.g.vs_battle_actors(), target.?.target.targetActor).*.?;
    const actor = psx.get(slot.stats).?;

    // hitParams has 2 entries; the C does not check hitIndex.
    const factor: c_int = @as(c_int, psx.getBits(psx.at(&action.?.hitParams, hitIndex), "statFactor"));
    const maxMP: c_int = actor.maxMP;
    const currentMP: c_int = actor.currentMP;

    const scaled: c_int = @divTrunc((maxMP -% currentMP) *% factor, 10);
    const level: c_uint = v.g.vs_battle_actionState().*.?.unk0 -% 1;
    const sum: c_uint = psx.conv(c_uint, scaled) +% level;
    return psx.conv(i16, sum);
}
