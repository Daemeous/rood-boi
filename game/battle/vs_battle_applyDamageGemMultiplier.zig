//! Hit scaling from the gems on the attacker's weapon and the target's shield
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_applyDamageGemMultiplier (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Scales `arg1` by 100 plus 5 for a weapon gem and minus 5 for a shield gem, depending on the action's kind.
pub fn vs_battle_applyDamageGemMultiplier(action: ?*v.vs_action_t, arg1: c_short, arg2: ?*v.vs_battle_actor2, arg3: ?*v.vs_battle_actor2) callconv(.c) c_short {
    var var_t0: c_int = 100;
    var var_a0: c_int = 0;

    if (@as(c_int, psx.getBits(action.?, "type")) == c.actionTypeSpell) {
        var_a0 = 0;
        for (0..3) |i| {
            if (@as(c_int, arg2.?.weapon.gems[i].gemEffects) == 4) {
                var_a0 = 1;
            }
        }

        if (var_a0 != 0) {
            var_t0 +%= 5;
        }

        var_a0 = 0;
        for (0..3) |i| {
            if (@as(c_int, arg3.?.shield.gems[i].gemEffects) == 8) {
                var_a0 = 1;
            }
        }
    } else {
        var_a0 = 0;
        for (0..3) |i| {
            if (@as(c_int, arg2.?.weapon.gems[i].gemEffects) == 3) {
                var_a0 = 1;
            }
        }

        if (var_a0 != 0) {
            var_t0 +%= 5;
        }

        var_a0 = 0;
        for (0..3) |i| {
            if (@as(c_int, arg3.?.shield.gems[i].gemEffects) == 7) {
                var_a0 = 1;
            }
        }
    }

    if (var_a0 != 0) {
        var_t0 -%= 5;
    }

    return psx.conv(i16, @divTrunc(@as(c_int, arg1) *% var_t0, 100));
}
