//! An action effect: the target actor's shield PP restored, for a hit with hpEffect 1 or 3 (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");

/// orig: func_80084DA8 (src/BATTLE/BATTLE.PRG/146C.c)
/// When the current hit's hpEffect is 1 or 3 and the target actor has a shield, adds its current PP to the entity's shield PP (shieldPpEffect 1).
pub fn func_80084DA8(_: ?*v.vs_action_t, _: ?*v._hitEntity_t, target: ?*v._hitEntity_t, _: c_int, _: c_int) callconv(.c) c_int {
    const entity = target.?;
    const actor = psx.get(v.g.vs_battle_actors()[entity.unk0.targetActor].?.stats);
    const effect: c_int = psx.getBits(&v.g.D_800F19CC().*.?.unk8.unk4, "hpEffect");
    if (effect == 1 or psx.getBits(&v.g.D_800F19CC().*.?.unk8.unk4, "hpEffect") == 3) {
        if (actor.?.shield.base.id != 0) {
            const amount = psx.conv(i16, actor.?.shield.currentPp);
            entity.shieldPp = psx.conv(i16, @as(c_int, entity.shieldPp) +% amount);
            psx.setBits(entity, "shieldPpEffect", 1);
        }
    }
    return 0; // no return statement in the C (the result register's leftover)
}
