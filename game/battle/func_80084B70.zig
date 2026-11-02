//! An action effect: damage to the target's hp, recorded, with its status and DP/PP side effects (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_80084B70 (src/BATTLE/BATTLE.PRG/146C.c)
/// Adds _calculateStatChange's amount to the target's hp (hpEffect 3 when the change set D_800F1A0C, else 1). For a
/// target entity with no unk40: records it (func_80080000), passes on the target actor's status 0x8000 unless its
/// unk957 is 0x80, and with arg4 set wears DP and PP (_adjustDpPp, func_800803A4).
pub fn func_80084B70(action: ?*v.vs_action_t, source: ?*v._hitEntity_t, target: ?*v._hitEntity_t, hit: c_int, arg4: c_int) callconv(.c) c_int {
    const actors = v.g.vs_battle_actors();
    const source_stats = psx.get(actors[source.?.unk0.targetActor].?.stats);
    const target_stats = psx.get(actors[target.?.unk0.targetActor].?.stats);
    const critical = psx.global(c_int, "D_800F1A0C");
    critical.* = 0;
    const amount = c._calculateStatChange(@ptrCast(action), @ptrCast(source), @ptrCast(target), hit, arg4);
    const entity = target.?;
    entity.unk0.hp = psx.conv(i16, @as(c_int, entity.unk0.hp) +% amount);
    if (critical.* != 0) {
        psx.setBits(entity, "hpEffect", 3);
    } else {
        psx.setBits(entity, "hpEffect", 1);
    }
    if (entity.unk40 == 0) {
        c.func_80080000(@ptrCast(action), @ptrCast(target), amount);
        if (target_stats.?.unk957 != 0x80) {
            entity.inflictStatuses |= target_stats.?.statuses & 0x8000;
        }
        if (arg4 != 0) {
            c._adjustDpPp(@ptrCast(action), @ptrCast(source_stats), @ptrCast(target_stats), amount);
            c.func_800803A4(@ptrCast(action), @ptrCast(source_stats), @ptrCast(target_stats), amount);
        }
    }
    return 0; // no return statement in the C (the result register's leftover)
}
