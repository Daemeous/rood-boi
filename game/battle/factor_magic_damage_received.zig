//! Damage factor for a spell received, from the attacker's stats and the action's stat factor (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: _factorMagicDamageReceived (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Scales a spell's damage by the stat factor of its hit parameters, when the current action is a spell with an HP effect.
pub fn factorMagicDamageReceived(action: ?*v.vs_action_t, _: ?*v._hitEntity_t, _: ?*v._hitEntity_t, arg3: c_int, _: c_int) callconv(.c) c_short {
    var result: i16 = 0;
    const current = v.g.vs_battle_actionState().*.?;
    if (@as(c_int, psx.getBits(psx.at(v.g.actions(), current.unk8.actionIndex), "type")) == c.actionTypeSpell) {
        const effect = &current.unk8.primaryTarget;
        if (psx.getBits(effect, "hpEffect") == 1 or psx.getBits(effect, "hpEffect") == 3) {
            const hp: c_int = @as(c_int, current.unk8.primaryTarget.target.hp);
            const mp: c_int = @as(c_int, current.unk8.primaryTarget.target.mp);
            const statFactor: c_int = @as(c_int, psx.getBits(psx.at(&action.?.hitParams, arg3), "statFactor"));
            const product: c_int = (hp +% mp) *% statFactor;
            result = psx.conv(i16, @divTrunc(product, 10));
        }
    }
    return result;
}
