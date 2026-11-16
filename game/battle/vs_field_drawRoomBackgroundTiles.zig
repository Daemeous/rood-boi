//! The scrolling sky backdrop: its tiles as textured Gouraud quads, then the black full-screen tile, as draw items
//! (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");
const sprite_item = common.sprite_item;

fn xy(x: c_int, y: c_int) c_int {
    return @bitCast((@as(c_uint, @bitCast(x)) & 0xFFFF) | (@as(c_uint, @bitCast(y)) << 16));
}

fn rgbOf(color: anytype) c_int {
    return @as(c_int, color.r) | (@as(c_int, color.g) << 8) | (@as(c_int, color.b) << 16);
}

/// orig: vs_field_drawRoomBackgroundTiles (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Rotates the sky tile grid by the camera yaw, emits each visible tile as a GT4 primitive, then the backdrop as a TILE.
pub fn vs_field_drawRoomBackgroundTiles(arg0: ?*v.vs_battle_backgroundLayout, scrollX: c_int, scrollY: c_int, tpage: c_int) callconv(.c) void {
    const scratch = common.scratch.get();
    const cosStep: c_int = c.rcos(v.g.vs_view().camera.angles.vz) *% 32;
    const sinStep: c_int = c.rsin(v.g.vs_view().camera.angles.vz) *% 32;
    // arg0 is only dereferenced from the loop condition on, after the two calls above.
    const layout = arg0.?;
    const tiles = &layout.tiles;
    var row: c_int = 0;

    while (row < layout.height *% 32) : (row +%= 32) {
        const y = row -% scrollY;
        var col: c_int = 0;
        while (col < 1024) : (col +%= 32) {
            const x = ((col +% scrollX) & 1023) -% 512;
            const screenX = c.rcos(v.g.vs_view().camera.angles.vz) *% x -% c.rsin(v.g.vs_view().camera.angles.vz) *% y;
            const screenY = c.rsin(v.g.vs_view().camera.angles.vz) *% x +% c.rcos(v.g.vs_view().camera.angles.vz) *% y;

            // Widescreen widens the sky limits by 54 units (54 << 12).
            const wide: c_int = if (v.g.pc_gte_widescreen().* != 0) 54 else 0;
            if (screenX < @as(c_int, -0x40000) -% (wide << 12) or
                screenX > @as(c_int, 0x180000) +% (wide << 12) or
                screenY < -0x40000 or
                screenY > 0x130000)
            {
                continue;
            }

            const tileIndex: c_int = (@divTrunc(row, 32) *% layout.width) +% @rem(@divTrunc(col, 32), layout.width);
            const tile = psx.at(tiles, tileIndex);
            if (tile.uv.w == 0 and tile.uv.h == 0) {
                continue;
            }

            const x0 = @divTrunc(screenX, 4096);
            const y0 = @divTrunc(screenY, 4096);
            const corners = [4]c_int{
                xy(x0, y0),
                xy(@divTrunc(screenX +% cosStep, 4096), @divTrunc(screenY +% sinStep, 4096)),
                xy(@divTrunc(screenX -% sinStep, 4096), @divTrunc(screenY +% cosStep, 4096)),
                xy(@divTrunc((screenX +% cosStep) -% sinStep, 4096), @divTrunc((screenY +% sinStep) +% cosStep, 4096)),
            };
            const rgb = [4]c_int{ rgbOf(tile.colors[0]), rgbOf(tile.colors[1]), rgbOf(tile.colors[2]), rgbOf(tile.colors[3]) };
            const ux: c_int = tile.uv.x;
            const uy: c_int = tile.uv.y;
            const uw: c_int = tile.uv.w;
            const uh: c_int = tile.uv.h;
            const texels = [4]c_int{
                (ux & 0xFF) | ((uy & 0xFF) << 8),
                ((ux +% uw) & 0xFF) | ((uy & 0xFF) << 8),
                (ux & 0xFF) | (((uy +% uh) & 0xFF) << 8),
                ((ux +% uw) & 0xFF) | (((uy +% uh) & 0xFF) << 8),
            };
            const page: c_int = (tpage | (@as(c_int, tile.colors[1].cd) << 7)) & 0xFFFF;
            _ = sprite_item.gt4Plain(psx.byteAt(u8, psx.get(scratch.unk4).?, 0x1FFC), false, page, 0x3BB0, &rgb, &corners, &texels);
        }
    }

    // The backdrop behind it: a black tile over the whole screen.
    sprite_item.tileFlat(psx.byteAt(u8, psx.get(scratch.unk4).?, 0x1FFC), false, 0, 0, 320 | (240 << 16));
}
