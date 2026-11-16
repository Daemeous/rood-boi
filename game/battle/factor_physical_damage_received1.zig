//! Physical damage a hit deals from the target's HP and MP, scaled by the action's stat factor
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: _factorPhysicalDamageReceived1 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Scales the summed HP and MP of the hit target by hit `arg3`'s stat factor; zero for spell actions.
pub fn factorPhysicalDamageReceived1(action: ?*v.vs_action_t, _: ?*v._hitEntity_t, _: ?*v._hitEntity_t, arg3: c_int, _: c_int) callconv(.c) c_short {
    var var_a2: c_short = 0;
    const d = v.g.vs_battle_actionState().*.?;
    // vs_main_actions[actionIndex]: a u16 index into 256 entries, not checked by the C.
    const a = psx.at(v.g.actions(), d.unk8.actionIndex);
    if (@as(c_int, psx.getBits(a, "type")) != c.actionTypeSpell) {
        const hpEffect = psx.getBits(&d.unk8.primaryTarget, "hpEffect");
        if (hpEffect == 1 or hpEffect == 3) {
            const hp: c_int = @as(c_int, d.unk8.primaryTarget.target.hp);
            const mp: c_int = @as(c_int, d.unk8.primaryTarget.target.mp);
            // hitParams has 2 entries; arg3 is not checked by the C.
            const statFactor: c_int = @as(c_int, psx.getBits(psx.at(&action.?.hitParams, arg3), "statFactor"));
            const product: c_int = (hp +% mp) *% statFactor;
            var_a2 = psx.conv(c_short, @divTrunc(product, 10));
        }
    }
    return var_a2;
}
