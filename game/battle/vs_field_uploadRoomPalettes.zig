//! Loads the three CLUT slots from vs_field_roomPalettes and the palette after them (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_uploadRoomPalettes (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Loads 0x100 colours of vs_field_roomPalettes into each of CLUT slots 0..2 (at 0, 0x100, 0x200) and hands 0x300 on to vs_field_setGimmickPaletteTable.
pub fn vs_field_uploadRoomPalettes() callconv(.c) void {
    c.vs_main_loadClut(v.g.vs_field_roomPalettes().*.?, 0, 0, 0x100);
    c.vs_main_loadClut(psx.at(v.g.vs_field_roomPalettes().*.?, 0x100), 1, 0, 0x100);
    c.vs_main_loadClut(psx.at(v.g.vs_field_roomPalettes().*.?, 0x200), 2, 0, 0x100);
    c.vs_field_setGimmickPaletteTable(psx.at(v.g.vs_field_roomPalettes().*.?, 0x300));
}
