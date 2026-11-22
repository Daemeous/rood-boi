//! Draws a GIM image at its scale (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");
const sprite_item = common.sprite_item;

/// orig: vs_render_drawGimTiles (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// The image's tile grid at its position and scale (sprites at 4096, else textured quads), its ground colour around and under empty tiles; ROOD_PC: a full-screen image is anchored as the scene (pc_hw_anchor_packets).
pub fn vs_render_drawGimTiles(image_arg: ?*v.gim_t, clut: c_int, ot: [*c]c_uint) callconv(.c) void {
    const Anchor = *const fn (?*const anyopaque, ?*const anyopaque, c_int) callconv(.c) void;
    const image = image_arg.?;
    var alpha: c_int = image.unk3;
    var semi: bool = undefined;
    var background: c_int = undefined; // set when color is nonzero, the only case that reads it
    const tiles: [*]align(1) u16 = @ptrCast(&image.imageTable[4]); // the PS1's image + 0x200: past the table's first four words
    const scale: c_int = image.unkC & 0x3FFF;
    const columns: c_int = @as(*const [2]u8, @ptrCast(&image.unk4))[0];
    const rows: c_int = @as(*const [2]u8, @ptrCast(&image.unk4))[1];
    const scratch = v.g.vs_scratch();
    // ROOD_PC: the anchor's start.
    const image_start = psx.get(scratch.unk0);
    var j: c_int = @as(c_int, image.unk8) + 160;
    const origin_x: c_int = j - 160;
    // The columns' x and the rows' y (the pad's top 0x400 held them; a count is a byte, so 256 entries each).
    var column_table: [256]i16 = undefined;
    var row_table: [256]i16 = undefined;
    const column_x: [*]i16 = &column_table;
    const row_y: [*]i16 = &row_table;
    var i: c_int = 0;
    while (i <= columns) : (i += 1) {
        column_x[@intCast(i)] = psx.conv(i16, (((i * 2 - columns) *% scale) >> 7) +% j);
    }
    j = @as(c_int, image.unkA) + 120;
    i = 0;
    while (i <= rows) : (i += 1) {
        row_y[@intCast(i)] = psx.conv(i16, (((i * 2 - rows) *% scale *% 15) >> 13) +% j);
    }
    // ROOD_PC: a picture of text the player's pack has the words of (gim_text.cpp) is drawn as real text: what its pack's boxes cover is left out of the tiles,
    // and carriers of its lines are added where the tiles are (below).
    const Replace = *const fn (?*v.gim_t) callconv(.c) c_int;
    const replaced: bool = if (psx.weak(Replace, "pc_gim_text_replace")) |ask| ask(image) != 0 else false;
    const origin_y: c_int = j - 120;
    // ROOD_PC: the boxes of the replaced words (the picture's pixels); only what they cover is left out of the tiles, so its ornaments and background stay.
    const Boxes = *const fn (*[64]c_int) callconv(.c) c_int;
    var boxes: [64]c_int = undefined;
    const box_count: usize = if (replaced) (if (psx.weak(Boxes, "pc_gim_text_boxes")) |get| @intCast(@max(get(&boxes), 0)) else 0) else 0;
    const color: c_int = @as(u16, @bitCast(image.unkE));
    if (color != 0) {
        const r = ((color << 3) & 0xF8) * alpha;
        const g = ((color >> 2) & 0xF8) * alpha;
        const b = ((color >> 7) & 0xF8) * alpha;
        background = (r >> 7) | ((g >> 7) << 8) | ((b >> 7) << 16);
        semi = (v.g.D_800F51C8().* & 1) != 0; // (the colour word's command byte was 0x60 + 2 * it: a tile, semi-transparent when it is 1)
        i = row_y[0];
        if (i > 0) {
            sprite_item.tileFlat(ot, semi, background, 0, 320 | (i << 16));
        }
        i = row_y[@intCast(rows)];
        if (i < 240) {
            sprite_item.tileFlat(ot, semi, background, i << 16, 320 | (0xF00000 -% (i << 16)));
        }
        i = column_x[0];
        if (i > 0) {
            sprite_item.tileFlat(ot, semi, background, 0, i | 0xF00000);
        }
        i = column_x[@intCast(columns)];
        if (i < 320) {
            sprite_item.tileFlat(ot, semi, background, i & 0xFFFF, (320 - i) | 0xF00000);
        }
    }
    const quad_color: c_int = alpha | (alpha << 8) | (alpha << 16); // (the packet's command byte, 0x2C + 2 * D_800F51C8, is the item's flags)
    alpha |= @as(c_int, v.g.D_800F51C8().*) << 8;
    if (replaced) { // (added before the tiles: the ordering table draws the last added first, so the words come over what the tiles draw)
        const Words = *const fn (?*v.gim_t, c_int, c_int, c_int, c_int, c_int, c_int, [*c]c_uint, ?*const i16, ?*const i16) callconv(.c) void;
        if (psx.weak(Words, "pc_gim_text_words")) |words| words(image, columns, rows, scale, origin_x, origin_y, alpha, ot, null, null);
    }
    j = 0;
    while (j < rows) : (j += 1) {
        const top: c_int = row_y[@intCast(j)];
        if (top >= 240) {
            continue;
        }
        const bottom: c_int = row_y[@intCast(j + 1)];
        if (bottom < 0) {
            continue;
        }
        i = 0;
        while (i < columns) : (i += 1) {
            const left: c_int = column_x[@intCast(i)];
            if (left >= 320) {
                continue;
            }
            const right: c_int = column_x[@intCast(i + 1)];
            if (right < 0) {
                continue;
            }
            const tile: c_int = tiles[@intCast(i + j * columns)];
            if (tile != 0) {
                var vv: c_int = tile << 6;
                const u: c_int = vv & 0xC0;
                vv = (vv & 0x1F00) * 15;
                const tpage: c_int = ((@as(c_int, psx.getBits(image, "unk0_2")) & 3) << 7) | (1 << 5) | (0x100 >> 4) | (((((@as(c_int, image.unk2) - 18) << 6) + ((tile >> 2) & 0x1C0)) & 0x3FF) >> 6);
                const tpage_word: c_int = tpage | @as(c_int, @bitCast(@as(c_uint, 0xE1000000)));
                // The tile's rectangle (64 x 15 texels) minus the boxes of the words: what is left of it, as pieces.
                var pieces: [96][4]c_int = undefined;
                var piece_count: usize = 1;
                pieces[0] = .{ 0, 0, 64, 15 };
                var carved = false;
                var bi: usize = 0;
                while (bi < box_count) : (bi += 1) {
                    const bx0 = boxes[bi * 4] - 1 - i * 64;
                    const by0 = boxes[bi * 4 + 1] - 1 - j * 15;
                    const bx1 = boxes[bi * 4 + 2] + 1 - i * 64;
                    const by1 = boxes[bi * 4 + 3] - j * 15; // (no margin below: a rule under the words stays)
                    var next: [96][4]c_int = undefined;
                    var next_count: usize = 0;
                    var pi: usize = 0;
                    while (pi < piece_count) : (pi += 1) {
                        const r = pieces[pi];
                        if (bx1 <= r[0] or bx0 >= r[2] or by1 <= r[1] or by0 >= r[3] or next_count + 4 > next.len) {
                            if (next_count < next.len) {
                                next[next_count] = r;
                                next_count += 1;
                            }
                            continue;
                        }
                        carved = true;
                        const cy0 = @max(by0, r[1]);
                        const cy1 = @min(by1, r[3]);
                        if (cy0 > r[1]) {
                            next[next_count] = .{ r[0], r[1], r[2], cy0 };
                            next_count += 1;
                        }
                        if (cy1 < r[3]) {
                            next[next_count] = .{ r[0], cy1, r[2], r[3] };
                            next_count += 1;
                        }
                        if (bx0 > r[0]) {
                            next[next_count] = .{ r[0], cy0, bx0, cy1 };
                            next_count += 1;
                        }
                        if (bx1 < r[2]) {
                            next[next_count] = .{ bx1, cy0, r[2], cy1 };
                            next_count += 1;
                        }
                    }
                    pieces = next;
                    piece_count = next_count;
                }
                if (carved) {
                    var pi: usize = 0;
                    while (pi < piece_count) : (pi += 1) {
                        const r = pieces[pi];
                        if (scale == 4096) { // (1:1: a sprite of the piece's texels, as the whole tile is drawn)
                            _ = sprite_item.sprite(ot, alpha, (left + r[0]) | ((top + r[1]) << 16), ((r[3] - r[1]) << 16) | (r[2] - r[0]), tpage_word, (u + r[0]) | (vv + (r[1] << 8)) | clut);
                            continue;
                        }
                        const sx0 = left + @divTrunc((right - left) * r[0], 64);
                        const sx1 = left + @divTrunc((right - left) * r[2], 64);
                        const sy0 = top + @divTrunc((bottom - top) * r[1], 15);
                        const sy1 = top + @divTrunc((bottom - top) * r[3], 15);
                        var u_l = u + r[0];
                        var u_r = u + r[2];
                        u_l -= u_l >> 8;
                        u_r -= u_r >> 8;
                        const v0 = vv + (r[1] << 8);
                        const v1 = vv + (r[3] << 8);
                        const clut_word: c_uint = @bitCast(u | vv | clut);
                        const page_word: c_uint = @bitCast((u + 64 - ((u + 64) >> 8)) | vv | (tpage << 16));
                        const corners = [4]c_int{ (sx0 & 0xFFFF) | (sy0 << 16), sx1 | (sy0 << 16), (sx0 & 0xFFFF) | (sy1 << 16), sx1 | (sy1 << 16) };
                        const texels = [4]c_int{ u_l | v0, u_r | v0, u_l | v1, u_r | v1 };
                        const item = sprite_item.ft4(ot, false, (alpha & 0x100) != 0, @intCast(page_word >> 16), @intCast(clut_word >> 16), quad_color, &corners, &texels);
                        sprite_item.before(item, tpage_word & 0x3FFF);
                    }
                    // (the background after the pieces: the ordering table draws what was added last first, so the flat tile is under them)
                    if (color != 0 and (color != 0x8000 or (alpha >> 8) == 0)) {
                        sprite_item.tileFlat(ot, semi, background, (left & 0xFFFF) | (top << 16), (right -% left) | ((bottom -% top) << 16));
                    }
                } else if (scale == 4096) {
                    _ = sprite_item.sprite(ot, alpha, left | (top << 16), 0xF0040, tpage_word, u | vv | clut);
                } else {
                    // A textured quad of the tile's corners (x0, top), (right, top), (x0, bottom), (right, bottom): the texels as the words held
                    // them (the row bits spill into the CLUT and page halves, as they did), the draw mode set before it.
                    const x0 = left & 0xFFFF;
                    var u_right = u + 64;
                    u_right -= u_right >> 8;
                    const packed_bottom = bottom << 16;
                    const clut_word: c_uint = @bitCast(u | vv | clut);
                    const page_word: c_uint = @bitCast(u_right | vv | (tpage << 16));
                    const corners = [4]c_int{ x0 | (top << 16), right | (top << 16), x0 | packed_bottom, right | packed_bottom };
                    const texels = [4]c_int{ u | vv, u_right | vv, u | (vv + 0xF00), u_right | (vv + 0xF00) };
                    const item = sprite_item.ft4(ot, false, (v.g.D_800F51C8().* & 1) != 0, @intCast(page_word >> 16), @intCast(clut_word >> 16), quad_color, &corners, &texels);
                    sprite_item.before(item, tpage_word & 0x3FFF);
                }
            } else if (color != 0 and (color != 0x8000 or v.g.D_800F51C8().* == 0)) {
                sprite_item.tileFlat(ot, semi, background, (left & 0xFFFF) | (top << 16), (right -% left) | ((bottom -% top) << 16));
            }
        }
    }
    // ROOD_PC: a full-screen image is the scene (5BF94.c).
    var full = columns * rows > 0;
    i = 0;
    while (full and i < columns * rows) : (i += 1) {
        full = tiles[@intCast(i)] != 0;
    }
    if (psx.weak(Anchor, "pc_hw_anchor_packets")) |anchor| {
        if (full and column_x[0] <= 0 and column_x[@intCast(columns)] >= 320 and row_y[0] <= 0 and row_y[@intCast(rows)] >= 224) {
            anchor(image_start, psx.get(scratch.unk0), 0x13);
        }
    }
}
