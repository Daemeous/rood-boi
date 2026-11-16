//! Spell damage factor from the target's hit parameters (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: _factorMagicDamageReceivedOfType (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns a spell's damage factor: the hit's HP plus MP, times its stat factor, over 10; 0 unless the action is a spell whose hit effect matches.
pub fn factorMagicDamageReceivedOfType(action: ?*v.vs_action_t, _: ?*v._hitEntity_t, _: ?*v._hitEntity_t, arg3: c_int, _: c_int) callconv(.c) c_short {
    var a2: c_short = 0;
    const d = v.g.vs_battle_actionState().*.?;

    // vs_main_actions[actionIndex] is not bounds-checked by the C: psx.at keeps that.
    const actionType: c_int = psx.getBits(psx.at(v.g.actions(), d.unk8.actionIndex), "type");
    if (actionType == c.actionTypeSpell) {
        const hpEffect: c_int = psx.getBits(&d.unk8.primaryTarget, "hpEffect");
        if (hpEffect == actionType or hpEffect == 3) {
            const hp: c_int = d.unk8.primaryTarget.target.hp;
            const mp: c_int = d.unk8.primaryTarget.target.mp;
            const statFactor: c_int = psx.getBits(psx.at(&action.?.hitParams, arg3), "statFactor");
            const total: c_int = (hp +% mp) *% statFactor;
            a2 = psx.conv(c_short, @divTrunc(total, 10));
        }
    }
    return a2;
}
