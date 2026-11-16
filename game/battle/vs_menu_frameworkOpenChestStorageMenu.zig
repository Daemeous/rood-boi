//! Battle state helper: calls vs_menu_frameworkOpenGameMenu(7, 2), then stores a byte and sets a flag bit
//! (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_menu_frameworkOpenChestStorageMenu (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Calls vs_menu_frameworkOpenGameMenu(7, 2), stores `arg0 + 0x40` in D_800F4E6B and sets bit 0x400 of vs_menu_frameworkActionRestrictions.
pub fn vs_menu_frameworkOpenChestStorageMenu(arg0: c_int) callconv(.c) void {
    _ = c.vs_menu_frameworkOpenGameMenu(7, 2);
    v.g.D_800F4E6B().* = psx.conv(u8, @as(c_int, arg0) +% 0x40);
    v.g.vs_menu_frameworkActionRestrictions().* |= 0x400;
}
