//! Shifts class affinity between two classes on the shield and on each equipped armor (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: vs_battle_applyArmorElementEnchantment (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Moves `amount` of class affinity from class `second` to class `first` on the shield and on each armor that has an item.
pub fn vs_battle_applyArmorElementEnchantment(actor: ?*v.vs_battle_actor2, first: c_int, second: c_int, amount: c_int) callconv(.c) void {
    const self = actor.?;

    if (self.shield.base.id != 0) {
        // first and second are not checked against the 8 classes: the index is taken as the C takes it.
        const up = psx.at(&self.shield.classAffinityCurrent.affinity[0], first);
        up.* = psx.conv(i16, @as(c_int, up.*) +% amount);
        const down = psx.at(&self.shield.classAffinityCurrent.affinity[0], second);
        down.* = psx.conv(i16, @as(c_int, down.*) -% amount);
        psx.at(&self.shield.unk134, first).* = psx.conv(i16, amount);
        psx.at(&self.shield.unk134, second).* = psx.conv(i16, -%amount);
    }

    for (0..6) |i| {
        const armor = &self.limbs[i].armor;
        if (armor.armor.id != 0) {
            const up = psx.at(&armor.classAffinityCurrent.affinity[0], first);
            up.* = psx.conv(i16, @as(c_int, up.*) +% amount);
            const down = psx.at(&armor.classAffinityCurrent.affinity[0], second);
            down.* = psx.conv(i16, @as(c_int, down.*) -% amount);
            psx.at(&armor.unk88, first).* = psx.conv(i16, amount);
            psx.at(&armor.unk88, second).* = psx.conv(i16, -%amount);
        }
    }
}
