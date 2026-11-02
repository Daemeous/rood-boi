//! Draws a GIM image through a wipe (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800CC204 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Draws the image's 15-line tile rows clipped per line to a span that grows (unkC below 4096) or shrinks
/// (above) with each line's width, from the side unk3 names; the full image at 4096 (func_800CBBCC).
/// ROOD_PC: in widescreen the spans are spread over the 16:9 view and the sides filled with the ground colour.
pub fn func_800CC204(image_arg: ?*v.gim_t, clut: c_int, ot: [*c]c_uint) callconv(.c) void {
    const Recorded = *const fn (c_int) callconv(.c) c_int;
    const Anchor = *const fn (?*const anyopaque, ?*const anyopaque, c_int) callconv(.c) void;
    const image = image_arg.?;
    var left: [240]i16 = undefined;
    var right: [240]i16 = undefined;
    const tiles: [*]align(1) u16 = @ptrFromInt(@intFromPtr(image) + 0x200);
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
        c.func_800CBBCC(@ptrCast(image), clut, ot);
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
            if (tile != 0) {
                const tpage: c_int = ((mode & 3) << 7) | (1 << 5) | (0x100 >> 4) | ((((page + (tile >> 8)) << 6) & 0x3FF) >> 6);
                // draw: once, and again for the next pixel when x is odd (the C's goto).
                while (true) {
                    const prim = c.vs_battle_setSprite(0x80, x | (y << 16), if ((x & 1) != 0) 0x10001 else ((end - x) | 0x10000), ot);
                    prim[1] = @bitCast(tpage | @as(c_int, @bitCast(@as(c_uint, 0xE1000000))));
                    prim[4] = @bitCast((((tile & 3) << 6) + (x & 0x3F)) | (((((tile >> 2) & 0x1F) * 15) + @rem(y, 15)) << 8) | clut);
                    if ((x & 1) != 0) {
                        x += 1;
                        continue;
                    }
                    break;
                }
            } else if (color != 0) {
                c.vs_battle_addTile(ot, ((color & 0x1F) << 3) | ((color & 0x3E0) << 6) | ((color & 0x7C00) << 9) | 0x40000000, (x & 0xFFFF) | (y << 16), (end - 1) | (y << 16));
            }
        }
        column = 0;
        while (margin != 0 and color != 0 and column < 2) : (column += 1) {
            x = if (column != 0) 320 else -margin;
            end = if (column != 0) 320 + margin else 0;
            x = if (left[@intCast(y)] > x) left[@intCast(y)] else x;
            end = if (right[@intCast(y)] < end) right[@intCast(y)] else end;
            if (end > x) {
                c.vs_battle_addTile(ot, ((color & 0x1F) << 3) | ((color & 0x3E0) << 6) | ((color & 0x7C00) << 9) | 0x40000000, (x & 0xFFFF) | (y << 16), ((end - 1) & 0xFFFF) | (y << 16));
            }
        }
    }
    if (margin != 0) {
        // The C calls the weak hook without a test: absent, it crashes.
        psx.weak(Anchor, "pc_hw_anchor_packets").?(wipe_start, psx.get(v.g.vs_scratch().unk0), 20);
    }
}
