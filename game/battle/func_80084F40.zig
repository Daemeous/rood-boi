//! An action effect: the stat change to weapon DP (limbs[6].value0: the C's spelling of weaponDp) (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_80084F40 (src/BATTLE/BATTLE.PRG/146C.c)
/// Adds _calculateStatChange's amount to weapon DP; marks weaponDpEffect 2.
pub fn func_80084F40(action: ?*v.vs_action_t, source: ?*v._hitEntity_t, target: ?*v._hitEntity_t, hit: c_int, arg4: c_int) callconv(.c) c_int {
    const amount = c._calculateStatChange(@ptrCast(action), @ptrCast(source), @ptrCast(target), hit, arg4);
    const entity = target.?;
    // limbs[6] is one past the six limbs: the bytes of weaponDp and weaponPp.
    psx.at(&entity.limbs, 6).effects.value0 = psx.conv(i16, @as(c_int, psx.at(&entity.limbs, 6).effects.value0) +% amount);
    psx.setBits(entity, "weaponDpEffect", 2);
    return 0; // no return statement in the C (the result register's leftover)
}
