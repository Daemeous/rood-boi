//! Stores one effect file load's LBA, size, frame buffer slot and CLUT flag in the load table (src/BATTLE/BATTLE.PRG/6F014.c).
const psx = @import("common").psx;
const v = @import("views_6F014");

/// orig: vs_battle_configureEffectFbLoad (src/BATTLE/BATTLE.PRG/6F014.c)
/// Records load context `index`: its sector, size, frame buffer slot and whether it is a CLUT.
pub fn configureEffectFbLoad(lba: c_int, size: c_int, slot: c_int, isClut: c_int, index: c_int) callconv(.c) void {
    const table = v.g.D_800F5638();
    // index is not checked against the array bounds, as in the original.
    const ctx = psx.at(&table.loadContexts, index);
    ctx.lba = psx.conv(u16, lba);
    ctx.size = psx.conv(u16, size);
    psx.at(&table.slots, index).* = psx.conv(u16, slot);
    psx.at(&table.isClut, index).* = psx.conv(u16, isClut);
}
