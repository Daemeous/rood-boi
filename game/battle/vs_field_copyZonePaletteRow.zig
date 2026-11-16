//! Copies a run of 16-bit words into the vs_field_roomPalettes table (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const c = @import("c_146C");
const v = @import("views_146C");

/// orig: vs_field_copyZonePaletteRow (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Copies `count` 16-bit words from `src` into vs_field_roomPalettes at index row*256+col.
pub fn vs_field_copyZonePaletteRow(src: ?*anyopaque, row: c_int, col: c_int, count: c_int) callconv(.c) void {
    // Pointer arithmetic as C does it: the int index is sign-extended and scaled by sizeof(u16), wrapping.
    const base: usize = @intFromPtr(v.g.vs_field_roomPalettes().*);
    const index: c_int = row *% 256 +% col;
    const dest_addr: usize = base +% @as(usize, @bitCast(@as(isize, index) *% 2));
    c.vs_main_memcpy(@ptrFromInt(dest_addr), src, psx.conv(c_uint, count *% 2));
}
