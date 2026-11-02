//! Draws a GIM image fading through the frame buffer (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// getTPage(tp, abr, x, y) (libgpu's macro): a draw-mode word.
fn tpage(tp: c_int, abr: c_int, x: c_int, y: c_int) c_uint {
    return 0xE1000000 | @as(c_uint, @bitCast((((tp & 3) << 7) | ((abr & 3) << 5) | ((y & 0x100) >> 4) | ((x & 0x3FF) >> 6) | ((y & 0x200) << 2)) & 0x9FF));
}

/// orig: func_800CC600 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Below full brightness: per 64-pixel column, the image blended with the frame (sprites through a texture page of the buffer, its tiles in between, the frame copied back) at the image's brightness; at 128 the plain image (func_800CBBCC).
pub fn func_800CC600(image_arg: ?*v.gim_t, clut: c_int, ot: [*c]c_uint) callconv(.c) void {
    const image = image_arg.?;
    const tiles: [*]align(1) u16 = @ptrFromInt(@intFromPtr(image) + 0x200);
    const mode: c_int = psx.getBits(image, "unk0_2");
    var fb: c_int = 0;
    if (v.g.vs_main_frameBuf().* == 0) {
        fb = 320;
    }
    var alpha: c_int = image.unk3;
    const page: c_int = (@as(c_int, image.unk2) + @as(c_int, psx.getBits(image, "unk0_9")) - 16) << 6;
    if (alpha == 128) {
        c.func_800CBBCC(@ptrCast(image), clut, ot);
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
    var column: c_int = 0;
    while (column < columns) : (column += 1) {
        var prim = c.vs_battle_setSprite(384, (column * 64) | (top << 16), 64 | (height << 16), ot);
        prim[1] = tpage(2, 1, page, 256);
        c.func_800CC5C0(ot, fb);
        var y: c_int = height - 15;
        while (y >= 0) : (y -= 15) {
            var x: c_int = 32;
            while (x >= 0) : (x -= 32) {
                prim = c.vs_battle_setSprite(384, x | (y << 16), 0xF0020, ot);
                prim[1] = tpage(2, 2, page, 256);
                prim[4] = @bitCast(x | (y << 8));
                c.func_800CC580(ot, page);
                prim = c.vs_battle_setSprite(alpha, (x + (column * 64)) | ((top + y) << 16), 0xF0020, ot);
                prim[1] = tpage(2, 1, page, 256);
                prim[4] = @bitCast(x | (y << 8));
                c.func_800CC5C0(ot, fb);
            }
        }
        var x: c_int = @as(c_int, image.unkA) + 120;
        y = 0;
        while (y < dims[1]) : (y += 1) {
            const tile: c_int = tiles[@intCast(y * columns + column)];
            if (tile != 0) {
                prim = c.vs_battle_setSprite(128, (((y * 30 - height) >> 1) + x - top) << 16, 0xF0040, ot);
                const atlas = tile >> 2;
                const uv = ((tile << 6) & 192) | (((atlas & 31) * 15) << 8) | clut;
                const tx = ((@as(c_int, image.unk2) - 18) << 6) + (atlas & 448);
                prim[4] = @bitCast(uv);
                prim[1] = tpage(mode, 0, tx, 256);
            }
        }
        c.func_800CC580(ot, page);
        y = 0;
        while (y < height) : (y += 15) {
            x = 0;
            while (x < 64) : (x += 32) {
                prim = c.vs_battle_setSprite(inverse, (x + (column * 64)) | ((top + y) << 16), 0xF0020, ot);
                prim[1] = tpage(2, 0, fb + column * 64, 0);
                prim[4] = @bitCast(x | ((top + y) << 8));
                c.func_800CC5C0(ot, fb);
                prim = c.vs_battle_setSprite(128, x | (y << 16), 0xF0020, ot);
                prim[1] = tpage(2, 0, fb + column * 64, 0);
                prim[4] = @bitCast(x | ((top + y) << 8));
                c.func_800CC580(ot, page);
            }
        }
        c.vs_battle_addTile(ot, 0x60000000, 0, 64 | (height << 16));
        c.func_800CC580(ot, page);
    }
}
