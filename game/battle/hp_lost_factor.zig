//! The hit-point loss factor of an action's hit on its target (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: _hpLostFactor (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// The target's lost HP scaled by the hit's stat factor, plus the battle's base step.
pub fn hpLostFactor(arg0: ?*v.vs_action_t, arg1: ?*v._hitEntity_t, arg2: ?*v._hitEntity_t, arg3: c_int, arg4: c_int) callconv(.c) c_short {
    _ = arg2;
    _ = arg4;
    const slot = v.g.vs_battle_actors()[arg1.?.target.targetActor].?;
    const actor = psx.get(slot.stats).?;
    const action = arg0.?;
    // arg3 is not checked by the C: psx.at indexes as the C does.
    const hit = psx.at(&action.hitParams, arg3);
    const statFactor: c_int = psx.getBits(hit, "statFactor");
    const value: c_int = (@as(c_int, actor.maxHP) -% @as(c_int, actor.currentHP)) *% statFactor;
    const quotient: c_int = @divTrunc(value, 10);
    const base: c_uint = v.g.vs_battle_actionState().*.?.unk0 -% 1;
    return psx.conv(c_short, @as(c_uint, @bitCast(quotient)) +% base);
}
