//! Physical damage factor a target takes from the current action's hit parameters
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: _factorPhysicalDamageReceived0 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Scales the target's HP plus MP by the action's stat factor, but only when the target's HP effect is 1 or 3 and the action is not a spell.
pub fn factorPhysicalDamageReceived0(action: ?*v.vs_action_t, _: ?*v._hitEntity_t, _: ?*v._hitEntity_t, arg3: c_int, _: c_int) callconv(.c) c_short {
    var factor: i16 = 0;
    const d = v.g.vs_battle_actionState().*.?;

    // vs_main_actions[actionIndex] is not checked against 256: the C does not check either.
    const actionType = psx.getBits(psx.at(v.g.actions(), d.unk8.actionIndex), "type");
    if (@as(c_int, actionType) != c.actionTypeSpell) {
        const hpEffect = psx.getBits(&d.unk8.primaryTarget, "hpEffect");
        if (hpEffect == 1 or hpEffect == 3) {
            const hp: c_int = d.unk8.primaryTarget.target.hp;
            const mp: c_int = d.unk8.primaryTarget.target.mp;
            // hitParams has 2 entries; arg3 is not checked in the C either.
            const statFactor: c_int = @as(c_int, psx.getBits(psx.at(&action.?.hitParams, arg3), "statFactor"));
            factor = psx.conv(i16, @divTrunc((hp +% mp) *% statFactor, 10));
        }
    }
    return factor;
}
