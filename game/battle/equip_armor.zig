//! Equips an armor piece on one body part of the character (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_equipArmor (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Applies `armor` to the character's limb `bodyPart`, then refreshes the battle state.
pub fn equipArmor(bodyPart: c_int, armor: ?*v.vs_main_inventoryArmor) callconv(.c) void {
    const character = v.g.vs_battle_characterState().*.?;
    // &stats->limbs[bodyPart].armor: the C does not dereference stats here, so a NULL stats
    // gives NULL plus the offset, computed as integers.
    const statsAddr: usize = if (psx.get(character.stats)) |s| @intFromPtr(s) else 0;
    const limbOffset: usize = @bitCast(@as(isize, bodyPart) *% @as(isize, @sizeOf(v.vs_battle_uiEquipment_limb)));
    const addr: usize = statsAddr +% @offsetOf(v.vs_battle_actor2, "limbs") +% limbOffset +% @offsetOf(v.vs_battle_uiEquipment_limb, "armor");
    const limbArmor: *align(1) v.vs_battle_uiArmor = @ptrFromInt(addr);
    c.vs_battle_applyArmor(@ptrCast(limbArmor), if (armor) |a| psx.cptr(c.vs_main_inventoryArmor, a) else null);
    c.vs_battle_nop0(@ptrCast(psx.get(character.stats)));
    c.vs_items_removeEquipmentBoostOnEquip();
    c.vs_items_removeArmorEnchantmentOnEquip();
}
