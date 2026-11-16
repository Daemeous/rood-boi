//! Attack bonus from the attacker's gems: 20 when its drawn weapon has a gem matching the action (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: _getAttackGemBuff (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns 20 when the actor's drawn weapon has a gem effect matching the action (effect 2 for a spell, else 1), else 0.
pub fn getAttackGemBuff(action: ?*v.vs_action_t, actor: ?*v.vs_battle_actor2) callconv(.c) c_short {
    var hasGemBuff: c_int = 0;
    var gemBuff: c_int = 0;

    const a = actor.?;
    if ((v.g.vs_battle_actors()[a.actorId].?.weaponDrawn & 1) != 0) {
        if (@as(c_int, psx.getBits(action.?, "type")) == c.actionTypeSpell) {
            hasGemBuff = 0;
            var i: usize = 0;
            while (i < 3) : (i += 1) {
                if (a.weapon.gems[i].gemEffects == 2) {
                    hasGemBuff = 1;
                }
            }
        } else {
            hasGemBuff = 0;
            var i: usize = 0;
            while (i < 3) : (i += 1) {
                if (a.weapon.gems[i].gemEffects == 1) {
                    hasGemBuff = 1;
                }
            }
        }
        if (hasGemBuff != 0) {
            gemBuff = 20;
        }
    }
    return psx.conv(c_short, gemBuff);
}
