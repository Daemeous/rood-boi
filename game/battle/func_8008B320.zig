//! Loads the three CLUT slots from D_800F1BA8 and the palette after them (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_8008B320 (src/BATTLE/BATTLE.PRG/146C.c)
/// Loads 0x100 colours of D_800F1BA8 into each of CLUT slots 0..2 (at 0, 0x100, 0x200) and hands 0x300 on to func_80099960.
pub fn func_8008B320() callconv(.c) void {
    c.vs_main_loadClut(v.g.D_800F1BA8().*.?, 0, 0, 0x100);
    c.vs_main_loadClut(psx.at(v.g.D_800F1BA8().*.?, 0x100), 1, 0, 0x100);
    c.vs_main_loadClut(psx.at(v.g.D_800F1BA8().*.?, 0x200), 2, 0, 0x100);
    c.func_80099960(psx.at(v.g.D_800F1BA8().*.?, 0x300));
}
