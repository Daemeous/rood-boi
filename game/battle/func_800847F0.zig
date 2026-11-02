//! An action effect: heals (or hurts) the target by the stat change when the current action's unk2 is 6 (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_800847F0 (src/BATTLE/BATTLE.PRG/146C.c)
/// When the current action's unk2 is 6: adds _calculateStatChange's amount to the target's hp, marks hpEffect 2, records it (func_800801E0).
pub fn func_800847F0(action: ?*v.vs_action_t, source: ?*v._hitEntity_t, target: ?*v._hitEntity_t, hit: c_int, arg4: c_int) callconv(.c) c_int {
    if (v.g.D_800F19CC().*.?.unk8.unk2 == 6) {
        const amount = c._calculateStatChange(@ptrCast(action), @ptrCast(source), @ptrCast(target), hit, arg4);
        const entity = target.?;
        entity.unk0.hp = psx.conv(i16, @as(c_int, entity.unk0.hp) +% amount);
        psx.setBits(entity, "hpEffect", 2);
        c.func_800801E0(@ptrCast(action), @ptrCast(target), amount);
    }
    return 0; // no return statement in the C (the result register's leftover)
}
