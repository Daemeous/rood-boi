//! Draws a GIM image at its scale (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800CBBCC (src/BATTLE/BATTLE.PRG/5BF94.c)
/// The image's tile grid at its position and scale (sprites at 4096, else textured quads), its ground colour around and under empty tiles; ROOD_PC: a full-screen image is anchored as the scene (pc_hw_anchor_packets).
pub fn func_800CBBCC(image_arg: ?*v.gim_t, clut: c_int, ot: [*c]c_uint) callconv(.c) void {
    const Anchor = *const fn (?*const anyopaque, ?*const anyopaque, c_int) callconv(.c) void;
    const image = image_arg.?;
    var alpha: c_int = image.unk3;
    var background: c_int = undefined; // set when color is nonzero, the only case that reads it
    const tiles: [*]align(1) u16 = @ptrFromInt(@intFromPtr(image) + 0x200);
    const scale: c_int = image.unkC & 0x3FFF;
    const columns: c_int = @as(*const [2]u8, @ptrCast(&image.unk4))[0];
    const rows: c_int = @as(*const [2]u8, @ptrCast(&image.unk4))[1];
    const scratch = v.g.vs_scratch();
    // ROOD_PC: the anchor's start.
    const image_start = psx.get(scratch.unk0);
    var j: c_int = @as(c_int, image.unk8) + 160;
    // Two scratchpad tables below its end: the columns' x, then the rows' y.
    const column_x: [*]i16 = @ptrFromInt(@as(usize, @as(u32, @bitCast(@as(c_int, 0x1F800400) - ((columns + 1) << 1)))));
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
        background = (r >> 7) | ((g >> 7) << 8) | ((b >> 7) << 16) | ((@as(c_int, v.g.D_800F51C8().*) + 48) << 25);
        i = row_y[0];
        if (i > 0) {
            c.vs_battle_addTile(ot, background, 0, 320 | (i << 16));
        }
        i = row_y[@intCast(rows)];
        if (i < 240) {
            c.vs_battle_addTile(ot, background, i << 16, 320 | (0xF00000 -% (i << 16)));
        }
        i = column_x[0];
        if (i > 0) {
            c.vs_battle_addTile(ot, background, 0, i | 0xF00000);
        }
        i = column_x[@intCast(columns)];
        if (i < 320) {
            c.vs_battle_addTile(ot, background, i & 0xFFFF, (320 - i) | 0xF00000);
        }
    }
    const quad_color: c_int = alpha | (alpha << 8) | (alpha << 16) | ((@as(c_int, v.g.D_800F51C8().*) + 22) << 25);
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
                var prim: [*c]c_uint = undefined;
                if (scale == 4096) {
                    prim = c.vs_battle_setSprite(alpha, left | (top << 16), 0xF0040, ot);
                } else {
                    const x0 = left & 0xFFFF;
                    var u_right = u + 64;
                    u_right -= u_right >> 8;
                    const packed_bottom = bottom << 16;
                    prim = @ptrCast(@alignCast(psx.get(scratch.unk0).?));
                    prim[0] = (ot.* & 0xFFFFFF) | 0xA000000;
                    prim[2] = @bitCast(quad_color);
                    prim[3] = @bitCast(x0 | (top << 16));
                    prim[5] = @bitCast(right | (top << 16));
                    prim[6] = @bitCast(u_right | vv | (tpage << 16));
                    prim[7] = @bitCast(x0 | packed_bottom);
                    prim[8] = @bitCast(u | (vv + 0xF00));
                    prim[9] = @bitCast(right | packed_bottom);
                    prim[10] = @bitCast(u_right | (vv + 0xF00));
                    ot.* = (psx.addr(prim) << 8) >> 8;
                    psx.put(&scratch.unk0, @as(*anyopaque, @ptrCast(prim + 11)));
                }
                prim[1] = @bitCast(tpage | @as(c_int, @bitCast(@as(c_uint, 0xE1000000))));
                prim[4] = @bitCast(u | vv | clut);
            } else if (color != 0 and (color != 0x8000 or v.g.D_800F51C8().* == 0)) {
                c.vs_battle_addTile(ot, background, (left & 0xFFFF) | (top << 16), (right -% left) | ((bottom -% top) << 16));
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
