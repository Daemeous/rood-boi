//! Lays out a line of text as draw items (pc/src/draw_items.h), one run placed in the ordering table where the packets
//! were linked (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");

// draw_items.c, by name: a sprite item, the run's group (ui_text finds a word by it), a draw mode alone, the marker.
const Count = *const fn () callconv(.c) c_int;
const Sprite = *const fn (c_int, c_int, c_int, c_int, c_int, c_int, c_int, c_int, c_int, c_int, c_int) callconv(.c) void;
const Group = *const fn (c_int, c_int) callconv(.c) void;
const ModeItem = *const fn (c_int, c_int) callconv(.c) void;
const Place = *const fn (c_int, c_int, [*]u32, [*]u32) callconv(.c) [*]u32;

/// The info style's page and CLUT, as the printer's draw mode and sprites named them.
const mode: c_int = 0x0C;
const clut: c_int = 0x37F6;

/// orig: vs_battle_renderTextRawColor (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Adds the glyph and underline sprites for `text` at (x, y) in `color` as items, placed in the ordering table at `nextPrim`
/// (the slot they link at; null: vs_scratch.unk8), and moves the packet cursor past their marker. The draw mode (the info
/// style's page) is set by the first of them, or alone when the text has none, as the packets' first one did.
pub fn renderTextRawColor(text: [*c]const u8, x: c_int, color: c_int, nextPrim: ?*anyopaque) callconv(.c) void {
    layout(text, x, color, nextPrim);
}

fn layout(text: [*c]const u8, x: c_int, color: c_int, nextPrim: ?*anyopaque) void {
    const scratch = v.g.vs_scratch();
    const widths = v.g.vs_render_compactGlyphWidths();
    const kern = v.g.D_800EB708();
    const glyph = v.g.D_800EB7D4();

    const y: c_int = x >> 16;
    var cx: c_int = psx.conv(i16, x);
    var next: ?*anyopaque = nextPrim;
    if (next == null) next = psx.get(scratch.unk8);

    // The glyphs in the order the string sets them, added last first (items of one depth draw the later one first).
    var glyphs: [256][6]c_int = undefined;
    var n: usize = 0;

    var tp: [*]const u8 = text;
    while (true) {
        var c: u8 = tp[0];
        tp += 1;
        if (c == 0) break;

        c -%= 0x20;
        if (c >= 0x3B) {
            if (c == 0x3B) cx -%= 1;
        } else if (psx.conv(c_uint, @as(c_int, c) -% 3) < 2) {
            var w: c_uint = 0;
            var scan: [*]const u8 = tp;
            while (true) {
                const raw = scan[0];
                scan += 1;
                if (raw == 0) break;
                var ch: u8 = raw;
                ch -%= 0x20;
                if (ch < 0x3C) {
                    const sh: u5 = @intCast((@as(c_int, ch) << 2) & 4);
                    w +%= psx.conv(c_uint, (@as(c_int, widths[@as(usize, ch >> 1)]) >> sh) & 0xF);
                    ch -%= 0x21;
                    if (ch < 26) {
                        var index: c_uint = psx.conv(c_uint, @as(c_int, scan[0]) - 0x41);
                        if (index < 26) {
                            index +%= @as(c_uint, ch) * 26;
                            const sh2: u5 = @intCast((index & 3) * 2);
                            w -%= psx.conv(c_uint, (@as(c_int, kern[@as(usize, index >> 2)]) >> sh2) & 3);
                        }
                    }
                }
            }
            const sh3: u5 = @intCast(@as(c_int, c) - 3);
            cx -%= psx.conv(c_int, w >> sh3);
        } else {
            const sh: u5 = @intCast((@as(c_int, c) << 2) & 4);
            const w: c_uint = psx.conv(c_uint, (@as(c_int, widths[@as(usize, c >> 1)]) >> sh) & 0xF);
            if (w != 0) {
                const index: c_uint = @as(c_uint, glyph[@as(usize, c)]);
                if (n < glyphs.len) {
                    const iw: c_int = @intCast(w);
                    if (c == 10) {
                        glyphs[n] = .{ cx -% 1, y -% 1, iw, 11, 0xE8, 0x4B };
                    } else {
                        glyphs[n] = .{ cx, y, iw, 10, @intCast(index & 0xFE), @intCast(((index & 1) *% 10) +% 0x4C) };
                    }
                    n += 1;
                }
                cx +%= psx.conv(c_int, w);
                c -%= 0x21;
                if (c < 26) {
                    var index2: c_uint = psx.conv(c_uint, @as(c_int, tp[0]) - 0x41);
                    if (index2 < 26) {
                        index2 +%= @as(c_uint, c) * 26;
                        const sh2: u5 = @intCast((index2 & 3) * 2);
                        cx -%= (@as(c_int, kern[@as(usize, index2 >> 2)]) >> sh2) & 3;
                    }
                }
            }
        }
    }

    const count_fn = psx.weak(Count, "pc_frame_count").?;
    const sprite = psx.weak(Sprite, "pc_frame_sprite").?;
    const rgb: c_int = color & 0xFFFFFF;
    const first = count_fn();
    while (n > 0) {
        n -= 1;
        const g = glyphs[n];
        sprite(0, g[0], g[1], g[2], g[3], g[4], g[5], rgb, clut, mode, 0);
    }
    // (the packets' draw mode was always written, even for a string with no glyph)
    if (count_fn() == first) {
        if (psx.weak(ModeItem, "pc_frame_mode_item")) |mode_item| mode_item(0, mode);
    }
    const count = count_fn() - first;
    if (psx.weak(Group, "pc_frame_group")) |group| group(first, count);
    const place = psx.weak(Place, "pc_frame_place").?;
    const cursor: [*]u32 = @ptrCast(@alignCast(psx.get(scratch.unk0).?));
    psx.put(&scratch.unk0, @as(*anyopaque, @ptrCast(place(first, count, @ptrCast(@alignCast(next.?)), cursor))));
}
