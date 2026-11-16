//! An action effect: heals (or hurts) the target by the stat change when the current action is not a spell (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_queueNonSpellTriggeredRecovery (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// When the current action is not a spell: adds _calculateStatChange's amount to the target's hp, marks hpEffect 2, records it (vs_battle_distributeLimbRecovery).
pub fn vs_battle_queueNonSpellTriggeredRecovery(action: ?*v.vs_action_t, source: ?*v._hitEntity_t, target: ?*v._hitEntity_t, hit: c_int, arg4: c_int) callconv(.c) c_int {
    if (psx.getBits(psx.at(v.g.actions(), v.g.vs_battle_actionState().*.?.unk8.actionIndex), "type") != c.actionTypeSpell) {
        const amount = c._calculateStatChange(@ptrCast(action), @ptrCast(source), @ptrCast(target), hit, arg4);
        const entity = target.?;
        entity.unk0.hp = psx.conv(i16, @as(c_int, entity.unk0.hp) +% amount);
        psx.setBits(entity, "hpEffect", 2);
        c.vs_battle_distributeLimbRecovery(@ptrCast(action), @ptrCast(target), amount);
    }
    return 0; // no return statement in the C (the result register's leftover)
}
