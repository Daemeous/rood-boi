//! Previous hit factor for an action's second hit parameter (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: _previousHitFactor2 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Scales the current HP plus MP of the first party slot by an action's second stat factor, plus a base offset.
pub fn previousHitFactor2(arg0: ?*v.vs_action_t, _: ?*v._hitEntity_t, _: ?*v._hitEntity_t, arg3: c_int, _: c_int) callconv(.c) c_short {
    const d = v.g.vs_battle_actionState().*.?;
    const hp: c_int = @as(c_int, d.unk8.targets[0].target.hp);
    const mp: c_int = @as(c_int, d.unk8.targets[0].target.mp);
    // arg3 is not checked against the two hit parameters: the index is taken as C does.
    const hit = psx.at(&arg0.?.hitParams, arg3);
    const statFactor: c_int = @as(c_int, psx.getBits(hit, "statFactor"));
    const scaled: c_int = @divTrunc((hp +% mp) *% statFactor, 10);
    const sum: c_uint = psx.conv(c_uint, scaled) +% (d.unk0 -% 1);
    return psx.conv(c_short, sum);
}
