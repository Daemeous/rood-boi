//! An action effect: the stat change to mp, capped at the target actor's current MP (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_80084CAC (src/BATTLE/BATTLE.PRG/146C.c)
/// Adds _calculateStatChange's amount to the entity's mp, capped at the target actor's current MP (the cap kept in _lastValue); mpEffect 1.
pub fn func_80084CAC(action: ?*v.vs_action_t, source: ?*v._hitEntity_t, target: ?*v._hitEntity_t, hit: c_int, arg4: c_int) callconv(.c) c_int {
    const stats = psx.get(v.g.vs_battle_actors()[target.?.unk0.targetActor].?.stats).?;
    var amount = c._calculateStatChange(@ptrCast(action), @ptrCast(source), @ptrCast(target), hit, arg4);
    if (stats.currentMP < amount) {
        amount = stats.currentMP;
        v.g._lastValue().* = psx.conv(u32, stats.currentMP);
    }
    const entity = target.?;
    entity.unk0.mp = psx.conv(i16, @as(c_int, entity.unk0.mp) +% amount);
    psx.setBits(entity, "mpEffect", 1);
    return 0; // no return statement in the C (the result register's leftover)
}
