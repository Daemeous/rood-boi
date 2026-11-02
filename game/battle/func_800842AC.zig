//! An action effect: armor DP twice the hp, also into the target limb's value1 (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");

/// orig: func_800842AC (src/BATTLE/BATTLE.PRG/146C.c)
/// Sets the entity's armorDp and its target limb's value1 to twice its hp (armorDpEffect 1).
pub fn func_800842AC(_: ?*v.vs_action_t, _: ?*v._hitEntity_t, target: ?*v._hitEntity_t, _: c_int, _: c_int) callconv(.c) c_int {
    const entity = target.?;
    entity.armorDp = psx.conv(i16, @as(c_int, entity.unk0.hp) *% 2);
    // targetLimb is a signed byte the C does not bound.
    psx.at(&entity.limbs, entity.unk0.targetLimb).effects.value1 = psx.conv(i16, @as(c_int, entity.unk0.hp) *% 2);
    psx.setBits(entity, "armorDpEffect", 1);
    return 0; // no return statement in the C (the result register's leftover)
}
