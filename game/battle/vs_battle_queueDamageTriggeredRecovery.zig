//! An action effect: heals (or hurts) the target by the stat change when the current hit's hpEffect is 1 or 3 (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_queueDamageTriggeredRecovery (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// When the current hit's hpEffect is 1 or 3: adds _calculateStatChange's amount to the target's hp, marks hpEffect 2, records it (vs_battle_distributeLimbRecovery).
pub fn vs_battle_queueDamageTriggeredRecovery(action: ?*v.vs_action_t, source: ?*v._hitEntity_t, target: ?*v._hitEntity_t, hit: c_int, arg4: c_int) callconv(.c) c_int {
    if ((blk: { const effect: c_int = psx.getBits(&v.g.vs_battle_actionState().*.?.unk8.primaryTarget, "hpEffect"); break :blk effect == 1 or effect == 3; })) {
        const amount = c._calculateStatChange(@ptrCast(action), @ptrCast(source), @ptrCast(target), hit, arg4);
        const entity = target.?;
        entity.target.hp = psx.conv(i16, @as(c_int, entity.target.hp) +% amount);
        psx.setBits(entity, "hpEffect", 2);
        c.vs_battle_distributeLimbRecovery(@ptrCast(action), @ptrCast(target), amount);
    }
    return 0; // no return statement in the C (the result register's leftover)
}
