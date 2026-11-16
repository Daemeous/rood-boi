//! Equips a weapon on the character: its category, range and attack shape
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_equipWeapon (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Equips `weapon` (null: unarmed) on the character and derives its range and attack shape.
pub fn equipWeapon(weapon: ?*v.vs_main_inventoryWeapon) callconv(.c) void {
    c.vs_battle_applyWeapon(
        @ptrCast(&psx.get(v.g.vs_battle_characterState().*.?.stats).?.weapon),
        @ptrCast(weapon),
    );
    c.vs_battle_nop0(@ptrCast(psx.get(v.g.vs_battle_characterState().*.?.stats)));

    if (weapon) |w| {
        // blade - 1 is -1 for blade 0: the C indexes before the array, psx.at does not check.
        v.g.vs_battle_characterState().*.?.equippedWeaponCategory =
            psx.at(&v.g.vs_main_inventory().blades, @as(c_int, w.blade) -% 1).category;
    } else {
        v.g.vs_battle_characterState().*.?.equippedWeaponCategory = 10;
    }

    const actor = psx.get(v.g.vs_battle_characterState().*.?.stats).?;
    const reach: c_int = @as(c_int, psx.getBits(actor, "reach"));
    psx.setBits(actor, "currentRange", psx.conv(u8, @as(c_int, actor.weapon.range.range) +% reach));
    psx.setBits(actor, "unk39", psx.conv(u8, @as(c_int, actor.weapon.range.unk1) +% reach));
    psx.setBits(actor, "unk3A", psx.conv(u8, @as(c_int, actor.weapon.range.unk2) +% reach));
    psx.setBits(actor, "currentAttackShapeAngle", psx.getBits(&actor.weapon.range, "attackShapeAngle"));
    psx.setBits(actor, "unk3B_0", 1);

    c.vs_items_removeEquipmentBoostOnEquip();
    c.vs_items_removeWeaponEnchantmentOnEquip();
}
