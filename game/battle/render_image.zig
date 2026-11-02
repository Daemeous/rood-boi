//! Uploads an image to VRAM (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_battle_renderImage (src/BATTLE/BATTLE.PRG/5BF94.c)
/// LoadImage of `buffer` into the next of eight RECTs (D_800F51D0, cursor D_800EC280), its x, y and w, h words from `xy` and `wh`.
pub fn renderImage(xy: c_int, buffer: ?*anyopaque, wh: c_int) callconv(.c) void {
    // Block-scope externs in the C (not in the unit's DWARF): reached by name.
    const cursor = psx.global(c_int, "D_800EC280");
    const rect = psx.at(psx.globalMany(v.RECT, "D_800F51D0"), cursor.*);
    cursor.* = (cursor.* +% 1) & 7;
    @as(*align(2) c_int, @ptrCast(&rect.x)).* = xy;
    @as(*align(2) c_int, @ptrCast(&rect.w)).* = wh;
    _ = c.LoadImage(@ptrCast(rect), @ptrCast(@alignCast(buffer)));
}
