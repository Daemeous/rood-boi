//! Draws one variable-width font glyph as a sprite and reports the x after it
//! (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");
const sprite_item = common.sprite_item;

const GlyphHook = *const fn (?*const anyopaque, c_int, c_int, c_int, c_int, c_int, c_int) callconv(.c) void;
const ClassicCharHook = *const fn (c_int, c_int, c_int) callconv(.c) void;
const ClassicMutedHook = *const fn () callconv(.c) c_int;

fn shl16(x: c_int) c_int {
    return @bitCast(@as(c_uint, @bitCast(x)) << 16);
}

/// orig: pc_text_char (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Hands a glyph to the text capture hook at 16.16 positions (ROOD_PC, a file-scope static),
/// unless the classic draw mutes it (its parity shadow's copies are not ui_text's).
fn pcTextChar(commands: ?*const anyopaque, count: c_int, glyph: c_int, x: c_int, y: c_int, advance: c_int) void {
    if (psx.weak(ClassicMutedHook, "pc_classic_text_muted")) |muted| {
        if (muted() != 0) return;
    }
    if (psx.weak(GlyphHook, "pc_text_glyph")) |hook| {
        hook(commands, count, glyph, shl16(x), shl16(y), shl16(x +% advance), advance);
    }
}

/// orig: vs_battle_printVariableWidthFontChar (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Draws font glyph `glyph_id` at (x, y) into the ordering table `ot` and returns the x after it.
pub fn printVariableWidthFontChar(glyph_id: c_uint, x: c_int, y: c_int, ot: [*c]c_uint) callconv(.c) c_int {
    // PC_CLASSIC (classic_hook.h): the glyph recorded as it is set.
    if (psx.weak(ClassicCharHook, "pc_classic_text_char")) |hook| hook(@bitCast(glyph_id), x, y);
    if (glyph_id == 143) {
        pcTextChar(null, 0, -1, x, y, 6);
        return x +% 6;
    }

    const glyphId: c_uint = glyph_id +% psx.conv(c_uint, @as(c_int, v.g.vs_battle_fontBank().*) *% 0xBD);

    const xy: c_uint = (@as(c_uint, @bitCast(x)) & 0xFFFF) | (@as(c_uint, @bitCast(y)) << 16);
    const glyphRow: c_int = psx.conv(c_int, glyphId / 21);
    const glyphCol: c_uint = glyphId % 21;
    const fontBits: c_uint = ((((@as(c_uint, v.g.D_800F4CB8().*) *% 0x10) +% 0x340) >> 4) & 0x3F) | 0x3780;
    const sprite = sprite_item.sprite(psx.at(ot, 3), v.g._fontBrightness().*, @bitCast(xy), 0xC000C, @bitCast(@as(c_uint, 0xE100002D)), @bitCast((glyphCol *% 0xC) |
        @as(c_uint, @bitCast(glyphRow *% 0xC00)) |
        (fontBits << 16)));

    const width: c_int = @as(c_int, v.g.vs_battle_characterWidths()[@as(usize, glyphId)]);
    // ui_text keys the glyph on its sprite's command word (an item's: draw_items.h).
    var commands = [1]?*anyopaque{@constCast(sprite_item.command(sprite))};
    pcTextChar(@ptrCast(&commands), 1, psx.conv(c_int, glyphId), x, y, width);

    return x +% width;
}
