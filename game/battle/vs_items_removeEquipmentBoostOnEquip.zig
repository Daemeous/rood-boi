//! Runs the status-0x100000 follow-up for the character state's actor (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_items_removeEquipmentBoostOnEquip (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Runs the status-0x100000 follow-up for the character state's actor.
pub fn vs_items_removeEquipmentBoostOnEquip() callconv(.c) void {
    const actor = psx.get(v.g.vs_battle_characterState().*.?.stats).?;
    if ((actor.statuses & 0x100000) != 0) {
        c.func_80093FEC(@as(c_int, actor.actorId), 0, 0x100000, 1);
        c.vs_battle_removeActorStatuses(0x100000, @ptrCast(actor));
    }
}
