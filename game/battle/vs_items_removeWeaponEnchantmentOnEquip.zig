//! Applies the character's status bits 21-24 through the status routines (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_items_removeWeaponEnchantmentOnEquip (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Passes the character's status bits 21-24 to func_80093FEC and vs_battle_removeActorStatuses when any is set.
pub fn vs_items_removeWeaponEnchantmentOnEquip() callconv(.c) void {
    const temp_s0 = psx.get(v.g.vs_battle_characterState().*.?.stats).?;

    if ((temp_s0.statuses & 0x01E00000) != 0) {
        // The status word is read again after the first call: the callee may have changed it.
        c.func_80093FEC(@as(c_int, temp_s0.actorId), 0, temp_s0.statuses & 0x01E00000, 1);
        c.vs_battle_removeActorStatuses(temp_s0.statuses & 0x01E00000, @ptrCast(temp_s0));
    }
}
