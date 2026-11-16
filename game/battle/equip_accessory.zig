//! Equips an accessory on the character in the character state (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_equipAccessory (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Puts `accessory` in the character's accessory slot, then refreshes the character and runs two follow-up updates.
pub fn equipAccessory(accessory: ?*v.vs_main_inventoryArmor) callconv(.c) void {
    c.vs_battle_applyAccessory(
        @ptrCast(&psx.get(v.g.vs_battle_characterState().*.?.stats).?.accessory),
        @ptrCast(accessory),
    );
    c.vs_battle_nop0(@ptrCast(psx.get(v.g.vs_battle_characterState().*.?.stats)));
    c.vs_items_removeEquipmentBoostOnEquip();
    c.vs_items_removeArmorEnchantmentOnEquip();
}
