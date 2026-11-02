//! An action effect: the target actor's weapon PP restored, when it has a blade (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");

/// orig: func_80084D44 (src/BATTLE/BATTLE.PRG/146C.c)
/// When the target actor has a blade, adds its weapon's current PP to the entity's weapon PP (limbs[6].value1; weaponPpEffect 1).
pub fn func_80084D44(_: ?*v.vs_action_t, _: ?*v._hitEntity_t, target: ?*v._hitEntity_t, _: c_int, _: c_int) callconv(.c) c_int {
    const entity = target.?;
    if (psx.get(v.g.vs_battle_actors()[entity.unk0.targetActor].?.stats).?.weapon.blade.id != 0) {
        const amount = psx.get(v.g.vs_battle_actors()[entity.unk0.targetActor].?.stats).?.weapon.currentPp;
        // limbs[6] is one past the six limbs: the bytes of weaponDp and weaponPp.
        const pp = &psx.at(&entity.limbs, 6).effects.value1;
        pp.* = psx.conv(i16, @as(c_int, pp.*) +% amount);
        psx.setBits(entity, "weaponPpEffect", 1);
    }
    return 0; // no return statement in the C (the result register's leftover)
}
