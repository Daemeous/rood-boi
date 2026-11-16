//! Adjusts a weapon's class affinity between two classes and records the deltas in unk168 (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: vs_battle_applyWeaponElementEnchantment (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Adds `value` to one class's weapon affinity, subtracts it from another, and stores the deltas in unk168; a weapon with no blade does nothing.
pub fn vs_battle_applyWeaponElementEnchantment(arg0: ?*v.vs_battle_actor2, arg1: c_int, arg2: c_int, arg3: c_int) callconv(.c) void {
    const actor = arg0.?;
    if (actor.weapon.blade.id != 0) {
        // Indices are unchecked in the original: psx.at reproduces the raw element address.
        const first = psx.at(&actor.weapon.classAffinityCurrent.affinity[0], arg1);
        first.* = psx.conv(i16, @as(c_int, first.*) +% arg3);

        const second = psx.at(&actor.weapon.classAffinityCurrent.affinity[0], arg2);
        second.* = psx.conv(i16, @as(c_int, second.*) -% arg3);

        psx.at(&actor.weapon.unk168, arg1).* = psx.conv(i16, arg3);
        psx.at(&actor.weapon.unk168, arg2).* = psx.conv(i16, -%arg3);
    }
}
