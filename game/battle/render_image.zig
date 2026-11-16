//! Uploads an image to VRAM (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_battle_renderImage (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// LoadImage of `buffer` into the next of eight RECTs (vs_render_legacyUploadRects, cursor vs_render_legacyUploadRectCursor), its x, y and w, h words from `xy` and `wh`.
pub fn renderImage(xy: c_int, buffer: ?*anyopaque, wh: c_int) callconv(.c) void {
    // Block-scope externs in the C (not in the unit's DWARF): reached by name.
    const cursor = psx.Global(c_int, "vs_render_legacyUploadRectCursor").ptr();
    const rect = psx.at(psx.GlobalMany(v.RECT, "vs_render_legacyUploadRects").ptr(), cursor.*);
    cursor.* = (cursor.* +% 1) & 7;
    @as(*align(2) c_int, @ptrCast(&rect.x)).* = xy;
    @as(*align(2) c_int, @ptrCast(&rect.w)).* = wh;
    _ = c.LoadImage(@ptrCast(rect), @ptrCast(@alignCast(buffer)));
}
