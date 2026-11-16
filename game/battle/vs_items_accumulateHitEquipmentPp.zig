//! Battle PP recovery for the attacker's weapon and the defender's shield, scaled by the amount and gem effects (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_items_accumulateHitEquipmentPp (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Restores the attacker's weapon PP (unless the action is a spell) and the defender's shield PP, by amount scaled per 10 and adjusted by gem effect 9.
pub fn vs_items_accumulateHitEquipmentPp(action: ?*v.vs_action_t, attacker: ?*v.vs_battle_actor2, defender: ?*v.vs_battle_actor2, amount: c_int) callconv(.c) void {
    var var_t0: c_int = 0;
    var var_t1: c_int = 100;

    if (@as(c_int, psx.getBits(action.?, "type")) != c.actionTypeSpell) {
        const a1 = attacker.?;
        var_t0 = 0;
        for (&a1.weapon.gems) |*gem| {
            if (gem.gemEffects == 9) var_t0 = 1;
        }

        if (var_t0 != 0) var_t1 +%= 5;

        var_t1 = @divTrunc(@divTrunc(amount, 10) *% var_t1, 100);
        const pp: c_int = @as(c_int, a1.weapon.currentPp) +% var_t1;
        if (pp < @as(c_int, a1.weapon.maxPp)) {
            a1.weapon.currentPp = psx.conv(u16, pp);
        } else {
            a1.weapon.currentPp = a1.weapon.maxPp;
        }
    }

    var_t1 = 100;

    const a2 = defender.?;
    const drawn = psx.at(v.g.vs_battle_actors(), a2.actorId).*.?.weaponDrawn;
    if ((drawn & 1) != 0) {
        var_t0 = 0;
        for (&a2.shield.gems) |*gem| {
            if (gem.gemEffects == 9) var_t0 = 1;
        }

        if (var_t0 != 0) var_t1 -%= 5;

        var_t1 = @divTrunc(@divTrunc(amount, 10) *% var_t1, 100);
        const pp: c_int = @as(c_int, a2.shield.currentPp) +% var_t1;
        if (pp < @as(c_int, a2.shield.maxPp)) {
            a2.shield.currentPp = psx.conv(u16, pp);
        } else {
            a2.shield.currentPp = a2.shield.maxPp;
        }
    }
}
