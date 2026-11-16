//! Draws a GIM image at its scale (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const pad = @import("common").pad;
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
    // Two scratchpad tables below its end: the columns' x, then the rows' y.
    const column_x: [*]i16 = @ptrFromInt(@as(usize, @as(u32, @bitCast(@as(c_int, @bitCast(pad.guest(0x400))) - ((columns + 1) << 1)))));
    const row_y: [*]i16 = column_x - @as(usize, @intCast(rows + 1));
    var i: c_int = 0;
    while (i <= columns) : (i += 1) {
        column_x[@intCast(i)] = psx.conv(i16, (((i * 2 - columns) *% scale) >> 7) +% j);
    }
    j = @as(c_int, image.unkA) + 120;
    i = 0;
    while (i <= rows) : (i += 1) {
        row_y[@intCast(i)] = psx.conv(i16, (((i * 2 - rows) *% scale *% 15) >> 13) +% j);
    }
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
                if (scale == 4096) {
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
