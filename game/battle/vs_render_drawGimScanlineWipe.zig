//! Draws a GIM image through a wipe (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");
const sprite_item = @import("common").sprite_item;

/// ROOD_PC: one line's segment [a, b) of a tile's texels (the original's sprite, the odd x drawn as its own pixel first).
fn wipeSegment(ot: [*c]c_uint, a: c_int, b: c_int, y: c_int, tile: c_int, tpage: c_int, clut: c_int) void {
    var x = a;
    while (true) {
        _ = sprite_item.sprite(ot, 0x80, x | (y << 16), if ((x & 1) != 0) 0x10001 else ((b - x) | 0x10000), @bitCast(tpage | @as(c_int, @bitCast(@as(c_uint, 0xE1000000)))), @bitCast((((tile & 3) << 6) + (x & 0x3F)) | (((((tile >> 2) & 0x1F) * 15) + @rem(y, 15)) << 8) | clut));
        if ((x & 1) != 0) {
            x += 1;
            continue;
        }
        break;
    }
}

/// orig: vs_render_drawGimScanlineWipe (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Draws the image's 15-line tile rows clipped per line to a span that grows (unkC below 4096) or shrinks
/// (above) with each line's width, from the side unk3 names; the full image at 4096 (vs_render_drawGimTiles).
/// ROOD_PC: in widescreen the spans are spread over the 16:9 view and the sides filled with the ground colour.
pub fn vs_render_drawGimScanlineWipe(image_arg: ?*v.gim_t, clut: c_int, ot: [*c]c_uint) callconv(.c) void {
    const Recorded = *const fn (c_int) callconv(.c) c_int;
    const Anchor = *const fn (?*const anyopaque, ?*const anyopaque, c_int) callconv(.c) void;
    const image = image_arg.?;
    var left: [240]i16 = undefined;
    var right: [240]i16 = undefined;
    const tiles: [*]align(1) u16 = @ptrCast(&image.imageTable[4]); // the PS1's image + 0x200: past the table's first four words
    const mode: c_int = psx.getBits(image, "unk0_2");
    const widths: [*]u16 = @ptrCast(&image.unk18);
    const page: c_int = @as(c_int, image.unk2) - 18;
    const color: c_int = @as(u16, @bitCast(image.unkE));
    var y: c_int = undefined;
    var column: c_int = undefined;
    var x: c_int = undefined;
    var end: c_int = undefined;
    // ROOD_PC: the widescreen wipe (5BF94.c).
    const wipe_start = psx.get(v.g.vs_scratch().unk0);
    var margin: c_int = 0;

    end = image.unk3;
    x = image.unkC & 0x3FFF;
    if (x == 4096) {
        image.unk3 = 0x80;
        c.vs_render_drawGimTiles(@ptrCast(image), clut, ot);
        image.unk3 = psx.conv(u8, end);
        return;
    }
    if (x < 4096) {
        y = 0;
        while (y < 240) : (y += 1) {
            column = (@as(c_int, widths[@intCast(y)]) * x) >> 12; // u16 * x < 4096: no overflow
            if (end != 0) {
                left[@intCast(y)] = psx.conv(i16, 320 - column);
                right[@intCast(y)] = 320;
            } else {
                left[@intCast(y)] = 0;
                right[@intCast(y)] = psx.conv(i16, column);
            }
        }
    } else {
        y = 0;
        while (y < 240) : (y += 1) {
            column = (@as(c_int, widths[@intCast(y)]) * (x - 4096)) >> 12; // u16 * (x - 4096) < 2^28: no overflow
            if (end != 0) {
                left[@intCast(y)] = 0;
                right[@intCast(y)] = psx.conv(i16, 320 - column);
            } else {
                left[@intCast(y)] = psx.conv(i16, column);
                right[@intCast(y)] = 320;
            }
        }
    }
    // ROOD_PC: a picture of text the player's pack has the words of (gim_text.cpp) is drawn as real text over this wipe, clipped to these spans (the 4:3 picture's, before
    // the widescreen spread); only what its boxes cover is left out of the picture's lines.
    const Replace = *const fn (?*v.gim_t) callconv(.c) c_int;
    const Words = *const fn (?*v.gim_t, c_int, c_int, c_int, c_int, c_int, c_int, [*c]c_uint, ?*const [240]i16, ?*const [240]i16) callconv(.c) void;
    const Boxes = *const fn (*[64]c_int) callconv(.c) c_int;
    const replaced: bool = if (psx.weak(Replace, "pc_gim_text_replace")) |ask| ask(image) != 0 else false;
    var boxes: [64]c_int = undefined;
    var box_count: usize = 0;
    if (replaced) {
        if (psx.weak(Words, "pc_gim_text_words")) |words| words(image, 5, 16, 4096, 0, 0, 0x80, ot, &left, &right);
        if (psx.weak(Boxes, "pc_gim_text_boxes")) |get| box_count = @intCast(@max(get(&boxes), 0));
    }
    if (v.g.pc_gte_widescreen().* != 0) {
        if (psx.weak(Recorded, "pc_hw_anchor_recorded")) |recorded| {
            if (recorded(20) != 0) { // PC_ANCHOR_SCENE
                margin = if (v.g.pc_gte_widescreen().* != 0) 54 else 0; // PC_WIDESCREEN_MARGIN
                y = 0;
                while (y < 240) : (y += 1) {
                    left[@intCast(y)] = psx.conv(i16, -margin + @divTrunc(@as(c_int, left[@intCast(y)]) * (320 + 2 * margin), 320));
                    right[@intCast(y)] = psx.conv(i16, -margin + @divTrunc(@as(c_int, right[@intCast(y)]) * (320 + 2 * margin), 320));
                }
            }
        }
    }
    y = 0;
    while (y < 240) : (y += 1) {
        column = 0;
        while (column < 5) : (column += 1) {
            x = left[@intCast(y)];
            if (x >= (column + 1) * 64) {
                continue;
            }
            end = right[@intCast(y)];
            if (end <= column * 64) {
                continue;
            }
            if (x < column * 64) {
                x = column * 64;
            }
            if (end > (column + 1) * 64) {
                end = (column + 1) * 64;
            }
            const tile: c_int = tiles[@intCast(@divTrunc(y, 15) * 5 + column)];
            // (the words' boxes on this line: [from, to) the picture's texels they replace)
            var cut: [16][2]c_int = undefined;
            var cut_n: usize = 0;
            var bi: usize = 0;
            while (bi < box_count) : (bi += 1) {
                if (y >= boxes[bi * 4 + 1] - 1 and y < boxes[bi * 4 + 3]) {
                    cut[cut_n] = .{ boxes[bi * 4] - 1, boxes[bi * 4 + 2] + 1 };
                    cut_n += 1;
                }
            }
            if (tile != 0) {
                const tpage: c_int = ((mode & 3) << 7) | (1 << 5) | (0x100 >> 4) | ((((page + (tile >> 8)) << 6) & 0x3FF) >> 6);
                var pieces: [20][2]c_int = undefined;
                pieces[0] = .{ x, end };
                var n: usize = 1;
                var ci: usize = 0;
                while (ci < cut_n) : (ci += 1) {
                    var next: [20][2]c_int = undefined;
                    var m: usize = 0;
                    var pi: usize = 0;
                    while (pi < n) : (pi += 1) {
                        const a = pieces[pi];
                        if (cut[ci][1] <= a[0] or cut[ci][0] >= a[1]) {
                            next[m] = a;
                            m += 1;
                            continue;
                        }
                        if (cut[ci][0] > a[0]) {
                            next[m] = .{ a[0], cut[ci][0] };
                            m += 1;
                        }
                        if (cut[ci][1] < a[1]) {
                            next[m] = .{ cut[ci][1], a[1] };
                            m += 1;
                        }
                        if (color != 0) { // (the picture's ground where the words were)
                            const f0 = @max(a[0], cut[ci][0]);
                            const f1 = @min(a[1], cut[ci][1]);
                            sprite_item.lineFlat(ot, false, ((color & 0x1F) << 3) | ((color & 0x3E0) << 6) | ((color & 0x7C00) << 9), (f0 & 0xFFFF) | (y << 16), ((f1 - 1) & 0xFFFF) | (y << 16));
                        }
                    }
                    pieces = next;
                    n = m;
                }
                var pi: usize = 0;
                while (pi < n) : (pi += 1) {
                    wipeSegment(ot, pieces[pi][0], pieces[pi][1], y, tile, tpage, clut);
                }
            } else if (color != 0) {
                sprite_item.lineFlat(ot, false, ((color & 0x1F) << 3) | ((color & 0x3E0) << 6) | ((color & 0x7C00) << 9), (x & 0xFFFF) | (y << 16), (end - 1) | (y << 16));
            }
        }
        column = 0;
        while (margin != 0 and color != 0 and column < 2) : (column += 1) {
            x = if (column != 0) 320 else -margin;
            end = if (column != 0) 320 + margin else 0;
            x = if (left[@intCast(y)] > x) left[@intCast(y)] else x;
            end = if (right[@intCast(y)] < end) right[@intCast(y)] else end;
            if (end > x) {
                sprite_item.lineFlat(ot, false, ((color & 0x1F) << 3) | ((color & 0x3E0) << 6) | ((color & 0x7C00) << 9), (x & 0xFFFF) | (y << 16), ((end - 1) & 0xFFFF) | (y << 16));
            }
        }
    }
    if (margin != 0) {
        // The C calls the weak hook without a test: absent, it crashes.
        psx.weak(Anchor, "pc_hw_anchor_packets").?(wipe_start, psx.get(v.g.vs_scratch().unk0), 20);
    }
}
