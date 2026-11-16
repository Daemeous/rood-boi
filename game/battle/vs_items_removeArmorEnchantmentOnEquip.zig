//! Character status effects: calls the two status routines for the character's statuses (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_items_removeArmorEnchantmentOnEquip (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Runs the status routines for the character's active status bits (0x1E000000).
pub fn vs_items_removeArmorEnchantmentOnEquip() callconv(.c) void {
    const actor = v.g.vs_battle_characterState().*.?;
    const temp = psx.get(actor.stats).?;

    if ((temp.statuses & 0x1E000000) != 0) {
        // The status bits are read again for the second call: the first may change them.
        c.func_80093FEC(@as(c_int, temp.unk957), 0, temp.statuses & 0x1E000000, 1);
        c.vs_battle_removeActorStatuses(temp.statuses & 0x1E000000, @ptrCast(temp));
    }
}
