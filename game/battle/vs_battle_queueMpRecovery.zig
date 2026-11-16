//! An action effect: the stat change to mp (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_queueMpRecovery (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Adds _calculateStatChange's amount to mp; marks mpEffect 2.
pub fn vs_battle_queueMpRecovery(action: ?*v.vs_action_t, source: ?*v._hitEntity_t, target: ?*v._hitEntity_t, hit: c_int, arg4: c_int) callconv(.c) c_int {
    const amount = c._calculateStatChange(@ptrCast(action), @ptrCast(source), @ptrCast(target), hit, arg4);
    const entity = target.?;
    entity.unk0.mp = psx.conv(i16, @as(c_int, entity.unk0.mp) +% amount);
    psx.setBits(entity, "mpEffect", 2);
    return 0; // no return statement in the C (the result register's leftover)
}
