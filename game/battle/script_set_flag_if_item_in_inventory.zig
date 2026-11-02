//! The event script's inventory test (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const c = @import("c_4A0A8");

/// orig: vs_battle_script_setFlagIfItemInInventory (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Sets the state flag getShort(script + 3) to whether the item getShort(script + 1) is in the inventory.
pub fn script_setFlagIfItemInInventory(script: [*c]u8, _: c_short) callconv(.c) c_int {
    // The object reads the item, tests it, then reads the flag.
    const held = c.vs_battle_itemIdIsInInventory(c.vs_battle_getShort(script + 1));
    c.vs_battle_setStateFlag(c.vs_battle_getShort(script + 3), psx.conv(u8, held));
    return 0;
}
