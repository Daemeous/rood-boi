//! A sprite as a draw item, for the Zig ports (pc/src/game/spriteItem.h: what vs_battle_setSprite and its kin made as packets).
const psx = @import("psx.zig");

const Item = *const fn (?*anyopaque, c_int, c_int, c_int, c_int, c_int) callconv(.c) c_int;
const Color = *const fn (c_int, c_int, c_int) callconv(.c) c_int;
const Command = *const fn (c_int) callconv(.c) ?*const anyopaque;

/// The draw mode of vs_battle_setSpriteDefaultTexPage's packet (the default texture page).
pub const page: c_int = @bitCast(@as(c_uint, 0xE100000C));

/// A sprite at `ot` (null: vs_scratch.unk8): `brightness` (bit 8: semi-transparent), place (x low, y high), size, draw mode as
/// an E1 word (0: the GPU's), texels and CLUT. Returns the item.
pub fn sprite(ot: ?*anyopaque, brightness: c_int, xy: c_int, wh: c_int, mode: c_int, uv_clut: c_int) c_int {
    return psx.weak(Item, "vs_battle_spriteItem").?(ot, brightness, xy, wh, mode, uv_clut);
}

/// The item's colour (0xBBGGRR) and whether it is semi-transparent.
pub fn color(item: c_int, rgb: c_int, semi: bool) void {
    _ = psx.weak(Color, "vs_battle_spriteColor").?(item, rgb, @intFromBool(semi));
}

/// The item's command word: what ui_text keys a glyph on.
pub fn command(item: c_int) ?*const anyopaque {
    return psx.weak(Command, "vs_battle_spriteCommand").?(item);
}

const Flat3 = *const fn (?*anyopaque, c_int, c_int, c_int, c_int) callconv(.c) void;

/// A flat tile at `ot`: `rgb` 0xBBGGRR, its place (x low, y high) and size (w low, h high), semi-transparent when `semi`.
pub fn tileFlat(ot: ?*anyopaque, semi: bool, rgb: c_int, xy: c_int, wh: c_int) void {
    psx.weak(Flat3, "vs_battle_tileFlatItem").?(ot, @intFromBool(semi), rgb, xy, wh);
}

/// A flat line at `ot` from `xy0` to `xy1` (x low, y high).
pub fn lineFlat(ot: ?*anyopaque, semi: bool, rgb: c_int, xy0: c_int, xy1: c_int) void {
    psx.weak(Flat3, "vs_battle_lineFlatItem").?(ot, @intFromBool(semi), rgb, xy0, xy1);
}

const Mode = *const fn (?*anyopaque, c_int) callconv(.c) void;

/// What vs_battle_insertTpage linked for a draw mode word (GP0 E1): the draw mode as an item at `ot`.
pub fn drawMode(ot: ?*anyopaque, e1: c_int) void {
    psx.weak(Mode, "vs_battle_modeItem").?(ot, e1);
}

const Flat = *const fn (?*anyopaque, c_int, c_int, c_int, [*]const c_int) callconv(.c) void;
const Shaded = *const fn (?*anyopaque, c_int, c_int, [*]const c_int, [*]const c_int) callconv(.c) void;
const DrawModeWindow = *const fn (?*anyopaque, c_int, c_int) callconv(.c) void;
const Mask = *const fn (?*anyopaque, c_int) callconv(.c) void;

/// A flat polygon (`verts` 3 or 4) at `ot`, `rgb` 0xBBGGRR, corners x low and y high.
pub fn flat(ot: ?*anyopaque, verts: c_int, semi: bool, rgb: c_int, xy: []const c_int) void {
    psx.weak(Flat, "vs_battle_flatItem").?(ot, verts, @intFromBool(semi), rgb, xy.ptr);
}

/// A shaded polygon: a colour per corner.
pub fn shaded(ot: ?*anyopaque, verts: c_int, semi: bool, rgb: []const c_int, xy: []const c_int) void {
    psx.weak(Shaded, "vs_battle_shadedItem").?(ot, verts, @intFromBool(semi), rgb.ptr, xy.ptr);
}

/// SetDrawMode's DR_MODE at `ot`: the draw mode (E1 bits; -1: none) and the texture window (E2's 20 bits).
pub fn drawModeWindow(ot: ?*anyopaque, e1: c_int, window: c_int) void {
    psx.weak(DrawModeWindow, "vs_battle_drawModeItem").?(ot, e1, window);
}

/// DR_STP at `ot`: the mask bit set when drawing.
pub fn mask(ot: ?*anyopaque, on: bool) void {
    psx.weak(Mask, "vs_battle_maskItem").?(ot, @intFromBool(on));
}

const Ft4 = *const fn (?*anyopaque, c_int, c_int, c_int, c_int, c_int, [*]const c_int, [*]const c_int) callconv(.c) c_int;

/// A textured quad at `ot`: its page word and CLUT, the colour its texels are scaled by (`raw`: drawn as they are), corners as x low
/// and y high, texels as u low and v high.
pub fn ft4(ot: ?*anyopaque, raw: bool, semi: bool, tpage: c_int, clut: c_int, rgb: c_int, xy: []const c_int, uv: []const c_int) c_int {
    return psx.weak(Ft4, "vs_battle_ft4Item").?(ot, @intFromBool(raw), @intFromBool(semi), tpage, clut, rgb, xy.ptr, uv.ptr);
}

const Area = *const fn (?*anyopaque, c_int, c_int, c_int, c_int, c_int, c_int, c_int) callconv(.c) void;

/// A drawing area at `ot` (x, y, w, h) with, when `offset` is given, the draw offset it sets.
pub fn area(ot: ?*anyopaque, x: c_int, y: c_int, w: c_int, h: c_int, offset: ?[2]c_int) void {
    const o = offset orelse [2]c_int{ 0, 0 };
    psx.weak(Area, "vs_battle_areaItem").?(ot, x, y, w, h, @intFromBool(offset != null), o[0], o[1]);
}

const LineG2 = *const fn (?*anyopaque, c_int, c_int, c_int, c_int, c_int) callconv(.c) c_int;
const Hidden = *const fn (c_int) callconv(.c) void;

/// A shaded line at `ot` between two points (a colour and a corner each), semi-transparent when `semi`; returns the item.
pub fn lineG2(ot: ?*anyopaque, semi: bool, rgb0: c_int, xy0: c_int, rgb1: c_int, xy1: c_int) c_int {
    return psx.weak(LineG2, "vs_battle_lineItem").?(ot, @intFromBool(semi), rgb0, xy0, rgb1, xy1);
}

/// The item is left out of the hardware picture and kept in the game's own.
pub fn hardwareHidden(item: c_int) void {
    psx.weak(Hidden, "vs_battle_itemHardwareHidden").?(item);
}

const Bracket = *const fn (c_int) callconv(.c) void;

/// The item is bracketed by the default draw mode as the effects' packets were (a draw mode set before it, the dithering one after it).
pub fn bracketed(item: c_int) void {
    psx.weak(Bracket, "vs_battle_itemBracket").?(item);
}

const Gt4 = *const fn (c_int, c_int, c_int, c_int, [*]const c_int, [*]const c_int, [*]const c_int) callconv(.c) void;

/// An effect trail's textured Gouraud quad at slot `depth` of the table vs_scratch.unk4, bracketed by the default draw mode as the
/// effect plugins' packets were: its page word and CLUT, a colour, corner (x low, y high) and texel (u low, v high) each.
pub fn gt4(depth: c_int, semi: bool, tpage: c_int, clut: c_int, rgb: []const c_int, xy: []const c_int, uv: []const c_int) void {
    psx.weak(Gt4, "vs_battle_gt4Item").?(depth, @intFromBool(semi), tpage, clut, rgb.ptr, xy.ptr, uv.ptr);
}

const Before = *const fn (c_int, c_int) callconv(.c) void;

/// The textured item is drawn after the draw mode `e1` (GP0 E1) is set, as the packet that began with that word drew: its dither and display bits.
pub fn before(item: c_int, e1: c_int) void {
    psx.weak(Before, "vs_battle_itemBefore").?(item, e1);
}

const Gt4Plain = *const fn (?*anyopaque, c_int, c_int, c_int, [*]const c_int, [*]const c_int, [*]const c_int) callconv(.c) c_int;

/// A textured Gouraud quad at `ot` (a GT4 packet): its page word and CLUT, the colour, place (x low, y high) and texels (u low, v high)
/// of each corner.
pub fn gt4Plain(ot: ?*anyopaque, semi: bool, tpage: c_int, clut: c_int, rgb: []const c_int, xy: []const c_int, uv: []const c_int) c_int {
    return psx.weak(Gt4Plain, "vs_battle_gt4PlainItem").?(ot, @intFromBool(semi), tpage, clut, rgb.ptr, xy.ptr, uv.ptr);
}

const LineMode = *const fn (?*anyopaque, c_int, c_int, c_int, c_int, c_int) callconv(.c) c_int;

/// A flat line at `ot`, in the draw mode `e1` (the E1 word set before it).
pub fn lineMode(ot: ?*anyopaque, semi: bool, rgb: c_int, xy0: c_int, xy1: c_int, e1: c_int) c_int {
    return psx.weak(LineMode, "vs_battle_lineModeItem").?(ot, @intFromBool(semi), rgb, xy0, xy1, e1);
}

const G4After = *const fn (?*anyopaque, c_int, c_int, [*]const c_int, [*]const c_int) callconv(.c) void;

/// A shaded quad at `ot` drawn in the draw mode `e1`, the mode `after` (GP0 E1 words) left set: a colour and a place (x low, y high) per corner.
pub fn g4After(ot: ?*anyopaque, e1: c_int, after: c_int, rgb: []const c_int, xy: []const c_int) void {
    psx.weak(G4After, "vs_battle_g4AfterItem").?(ot, e1, after, rgb.ptr, xy.ptr);
}
