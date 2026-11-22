//! Draws a GIM image fading through the frame buffer (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");
const sprite_item = @import("common").sprite_item;

/// getTPage(tp, abr, x, y) (libgpu's macro): a draw-mode word.
fn tpage(tp: c_int, abr: c_int, x: c_int, y: c_int) c_uint {
    return 0xE1000000 | @as(c_uint, @bitCast((((tp & 3) << 7) | ((abr & 3) << 5) | ((y & 0x100) >> 4) | ((x & 0x3FF) >> 6) | ((y & 0x200) << 2)) & 0x9FF));
}

/// orig: vs_render_drawGimCrossfade (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Below full brightness: per 64-pixel column, the image blended with the frame (sprites through a texture page of the buffer, its tiles in between, the frame copied back) at the image's brightness; at 128 the plain image (vs_render_drawGimTiles).
pub fn vs_render_drawGimCrossfade(image_arg: ?*v.gim_t, clut: c_int, ot: [*c]c_uint) callconv(.c) void {
    const image = image_arg.?;
    const tiles: [*]align(1) u16 = @ptrCast(&image.imageTable[4]); // the PS1's image + 0x200: past the table's first four words
    const mode: c_int = psx.getBits(image, "unk0_2");
    var fb: c_int = 0;
    if (v.g.vs_main_frameBuf().* == 0) {
        fb = 320;
    }
    var alpha: c_int = image.unk3;
    const page: c_int = (@as(c_int, image.unk2) + @as(c_int, psx.getBits(image, "unk0_9")) - 16) << 6;
    if (alpha == 128) {
        c.vs_render_drawGimTiles(@ptrCast(image), clut, ot);
        return;
    }
    var inverse: c_int = 128 - alpha;
    alpha |= 256;
    inverse += 7;
    alpha += 7;
    const dims: *const [2]u8 = @ptrCast(&image.unk4);
    const columns: c_int = dims[0];
    const height: c_int = @as(c_int, dims[1]) * 15;
    const top: c_int = @as(c_int, image.unkA) - ((height >> 1) - 120);
    // ROOD_PC: a picture of text the player's pack has the words of (gim_text.cpp): what its boxes cover is left out of the tiles (the rest is drawn as before) and
    // carriers of its lines are added, at the picture's brightness (the picture here is laid out from the left edge at its offset).
    const Replace = *const fn (?*v.gim_t) callconv(.c) c_int;
    const Words = *const fn (?*v.gim_t, c_int, c_int, c_int, c_int, c_int, c_int, [*c]c_uint, ?*const i16, ?*const i16) callconv(.c) void;
    const Boxes = *const fn (*[64]c_int) callconv(.c) c_int;
    const replaced: bool = if (psx.weak(Replace, "pc_gim_text_replace")) |ask| ask(image) != 0 else false;
    var boxes: [64]c_int = undefined;
    var box_count: usize = 0;
    if (replaced) {
        if (psx.weak(Boxes, "pc_gim_text_boxes")) |get| box_count = @intCast(@max(get(&boxes), 0));
        if (psx.weak(Words, "pc_gim_text_words")) |words| words(image, columns, dims[1], 4096, columns * 32 - 160, top + (height >> 1) - 120, @min(alpha - 263, 0x7F), ot, null, null); // (oy: the offset `top` came from; alpha: the picture's brightness, as it was before |= 256 and += 7)
    }
    var column: c_int = 0;
    while (column < columns) : (column += 1) {
        _ = sprite_item.sprite(ot, 384, (column * 64) | (top << 16), 64 | (height << 16), @bitCast(tpage(2, 1, page, 256)), 0);
        c.vs_render_setGimFramebufferDrawArea(ot, fb);
        var y: c_int = height - 15;
        while (y >= 0) : (y -= 15) {
            var x: c_int = 32;
            while (x >= 0) : (x -= 32) {
                _ = sprite_item.sprite(ot, 384, x | (y << 16), 0xF0020, @bitCast(tpage(2, 2, page, 256)), @bitCast(x | (y << 8)));
                c.vs_render_setGimWorkspaceDrawArea(ot, page);
                _ = sprite_item.sprite(ot, alpha, (x + (column * 64)) | ((top + y) << 16), 0xF0020, @bitCast(tpage(2, 1, page, 256)), @bitCast(x | (y << 8)));
                c.vs_render_setGimFramebufferDrawArea(ot, fb);
            }
        }
        var x: c_int = @as(c_int, image.unkA) + 120;
        y = 0;
        while (y < dims[1]) : (y += 1) {
            const tile: c_int = tiles[@intCast(y * columns + column)];
            if (tile != 0) {
                const atlas = tile >> 2;
                const uv = ((tile << 6) & 192) | (((atlas & 31) * 15) << 8) | clut;
                const tx = ((@as(c_int, image.unk2) - 18) << 6) + (atlas & 448);
                const at_y = ((y * 30 - height) >> 1) + x - top;
                // (the tile less the boxes of the words: pieces of it)
                var pieces: [32][4]c_int = undefined;
                pieces[0] = .{ 0, 0, 64, 15 };
                var n: usize = 1;
                var bi: usize = 0;
                while (bi < box_count) : (bi += 1) {
                    const bx0 = boxes[bi * 4] - 1 - column * 64;
                    const by0 = boxes[bi * 4 + 1] - 1 - y * 15;
                    const bx1 = boxes[bi * 4 + 2] + 1 - column * 64;
                    const by1 = boxes[bi * 4 + 3] - y * 15;
                    var next: [32][4]c_int = undefined;
                    var m: usize = 0;
                    var pi: usize = 0;
                    while (pi < n) : (pi += 1) {
                        const r = pieces[pi];
                        if (bx1 <= r[0] or bx0 >= r[2] or by1 <= r[1] or by0 >= r[3] or m + 4 > next.len) {
                            if (m < next.len) {
                                next[m] = r;
                                m += 1;
                            }
                            continue;
                        }
                        const cy0 = @max(by0, r[1]);
                        const cy1 = @min(by1, r[3]);
                        if (cy0 > r[1]) {
                            next[m] = .{ r[0], r[1], r[2], cy0 };
                            m += 1;
                        }
                        if (cy1 < r[3]) {
                            next[m] = .{ r[0], cy1, r[2], r[3] };
                            m += 1;
                        }
                        if (bx0 > r[0]) {
                            next[m] = .{ r[0], cy0, bx0, cy1 };
                            m += 1;
                        }
                        if (bx1 < r[2]) {
                            next[m] = .{ bx1, cy0, r[2], cy1 };
                            m += 1;
                        }
                    }
                    pieces = next;
                    n = m;
                }
                var pi: usize = 0;
                while (pi < n) : (pi += 1) {
                    const r = pieces[pi];
                    _ = sprite_item.sprite(ot, 128, r[0] | ((at_y + r[1]) << 16), ((r[3] - r[1]) << 16) | (r[2] - r[0]), @bitCast(tpage(mode, 0, tx, 256)), (uv & ~@as(c_int, 0xffff)) | ((uv & 0xff) + r[0]) | ((((uv >> 8) & 0xff) + r[1]) << 8));
                }
                if (n != 1 or pieces[0][0] != 0 or pieces[0][1] != 0 or pieces[0][2] != 64 or pieces[0][3] != 15) {
                    // (what the boxes took out was black texels of the picture too, which the blend darkens the frame by: the tile's own place, black, drawn before its pieces)
                    sprite_item.tileFlat(ot, false, 0, at_y << 16, 64 | (15 << 16));
                }
            }
        }
        c.vs_render_setGimWorkspaceDrawArea(ot, page);
        y = 0;
        while (y < height) : (y += 15) {
            x = 0;
            while (x < 64) : (x += 32) {
                _ = sprite_item.sprite(ot, inverse, (x + (column * 64)) | ((top + y) << 16), 0xF0020, @bitCast(tpage(2, 0, fb + column * 64, 0)), @bitCast(x | ((top + y) << 8)));
                c.vs_render_setGimFramebufferDrawArea(ot, fb);
                _ = sprite_item.sprite(ot, 128, x | (y << 16), 0xF0020, @bitCast(tpage(2, 0, fb + column * 64, 0)), @bitCast(x | ((top + y) << 8)));
                c.vs_render_setGimWorkspaceDrawArea(ot, page);
            }
        }
        sprite_item.tileFlat(ot, false, 0, 0, 64 | (height << 16));
        c.vs_render_setGimWorkspaceDrawArea(ot, page);
    }
}
