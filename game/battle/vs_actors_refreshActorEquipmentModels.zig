//! Re-applies a battle actor's weapon and shield to vs_actors_queueEquipmentModelLoad (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_actors_refreshActorEquipmentModels (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sends battle actor `arg0`'s weapon blade and shield base to vs_actors_queueEquipmentModelLoad.
pub fn vs_actors_refreshActorEquipmentModels(arg0: c_int) callconv(.c) void {
    const slot = psx.at(v.g.vs_battle_actors(), arg0).*;
    if (slot) |actor| {
        if (psx.get(actor.stats)) |stats| {
            if (stats.weapon.blade.id != 0) {
                c.vs_actors_queueEquipmentModelLoad(@ptrCast(actor), arg0, @as(c_int, stats.weapon.wepId), 0, @as(c_int, stats.weapon.blade.material));
            } else {
                c.vs_actors_queueEquipmentModelLoad(@ptrCast(actor), arg0, 0, 0, 0);
            }
            if (stats.shield.base.id != 0) {
                c.vs_actors_queueEquipmentModelLoad(@ptrCast(actor), arg0, @as(c_int, stats.shield.wepId), 1, @as(c_int, stats.shield.base.material));
            } else {
                c.vs_actors_queueEquipmentModelLoad(@ptrCast(actor), arg0, 0, 0, 0);
            }
        }
    }
}
