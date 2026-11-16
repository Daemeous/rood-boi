//! An action effect: the stat change to the target's hp, recorded for a target entity with no unk40 (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_queueHpRecovery (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Adds _calculateStatChange's amount to the target's hp and marks hpEffect 2; records it (vs_battle_distributeLimbRecovery) when unk40 is 0.
pub fn vs_battle_queueHpRecovery(action: ?*v.vs_action_t, source: ?*v._hitEntity_t, target: ?*v._hitEntity_t, hit: c_int, arg4: c_int) callconv(.c) c_int {
    const amount = c._calculateStatChange(@ptrCast(action), @ptrCast(source), @ptrCast(target), hit, arg4);
    const entity = target.?;
    entity.unk0.hp = psx.conv(i16, @as(c_int, entity.unk0.hp) +% amount);
    psx.setBits(entity, "hpEffect", 2);
    if (entity.unk40 == 0) {
        c.vs_battle_distributeLimbRecovery(@ptrCast(action), @ptrCast(target), amount);
    }
    return 0; // no return statement in the C (the result register's leftover)
}
