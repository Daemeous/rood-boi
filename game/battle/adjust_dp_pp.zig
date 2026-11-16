//! Battle DP adjustment: a hit takes DP from the attacker's weapon, the target's shield and its armor (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: _adjustDpPp (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Takes `amount` DP off the source's weapon (unless the action is a spell), the target's shield (when drawn) and each armor piece with an adjustment, floored at 0.
pub fn adjustDpPp(action: ?*v.vs_action_t, source: ?*v.vs_battle_actor2, target: ?*v.vs_battle_actor2, amount: c_int) callconv(.c) void {
    if (@as(c_int, psx.getBits(action.?, "type")) != c.actionTypeSpell) {
        const dp = &source.?.weapon.currentDp;
        if (@as(c_int, dp.*) >= amount) {
            dp.* = psx.conv(u16, @as(c_int, dp.*) -% amount);
        } else {
            dp.* = 0;
        }
    }

    if ((v.g.vs_battle_actors()[target.?.actorId].?.weaponDrawn & 1) != 0) {
        const dp = &target.?.shield.currentDp;
        if (@as(c_int, dp.*) >= amount) {
            dp.* = psx.conv(u16, @as(c_int, dp.*) -% amount);
        } else {
            dp.* = 0;
        }
    }

    for (0..6) |i| {
        const adj: c_int = @as(c_int, v.g._armorDpAdjustmentAmounts()[i]);
        if (adj != 0) {
            const dp = &target.?.limbs[i].armor.currentDp;
            if (@as(c_int, dp.*) >= adj) {
                dp.* = psx.conv(u16, @as(c_int, dp.*) -% adj);
            } else {
                dp.* = 0;
            }
        }
    }
}
