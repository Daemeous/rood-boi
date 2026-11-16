//! Battle unit 30DB0: copies a 0xE0-byte block into vs_field_gimmickPaletteColors (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c).
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: vs_field_setGimmickPaletteTable (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c)
/// Copies 0xE0 bytes from `src` into the block at vs_field_gimmickPaletteColors.
pub fn vs_field_setGimmickPaletteTable(src: ?*anyopaque) callconv(.c) void {
    c.vs_main_memcpy(@ptrCast(v.g.vs_field_gimmickPaletteColors()), src, 0xE0);
}
