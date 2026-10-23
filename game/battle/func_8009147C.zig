//! The scrolling sky backdrop: its tiles as GT4 polygons into the packet
//! buffer, then the black full-screen tile (src/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_8009147C (src/BATTLE/BATTLE.PRG/2842C.c)
/// Rotates the sky tile grid by the camera yaw, emits each visible tile as a GT4 primitive, then the backdrop as a TILE.
pub fn func_8009147C(arg0: ?*v.vs_battle_backgroundLayout, scrollX: c_int, scrollY: c_int, tpage: c_int) callconv(.c) void {
    const scratch: *v.vs_scratch_t = @ptrFromInt(0x1F800000);
    var buffer: [*]u8 = @ptrCast(psx.get(scratch.unk0).?);
    const cosStep: c_int = c.rcos(scratch.camera.angles.vz) *% 32;
    const sinStep: c_int = c.rsin(scratch.camera.angles.vz) *% 32;
    // arg0 is only dereferenced from the loop condition on, after the two calls above.
    const layout = arg0.?;
    const tiles = &layout.tiles;
    var row: c_int = 0;
    var prim: [*]align(1) v.POLY_GT4 = undefined;

    while (row < layout.height *% 32) : (row +%= 32) {
        const y = row -% scrollY;
        prim = @ptrCast(buffer);
        var col: c_int = 0;
        while (col < 1024) : (col +%= 32) {
            const x = ((col +% scrollX) & 1023) -% 512;
            const screenX = c.rcos(scratch.camera.angles.vz) *% x -% c.rsin(scratch.camera.angles.vz) *% y;
            const screenY = c.rsin(scratch.camera.angles.vz) *% x +% c.rcos(scratch.camera.angles.vz) *% y;

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

            const tag: *align(1) v.P_TAG = @ptrCast(&prim[0]);
            psx.setBits(tag, "len", 12);
            tag.code = 0x3C;

            prim[0].x0 = psx.conv(i16, @divTrunc(screenX, 4096));
            prim[0].y0 = psx.conv(i16, @divTrunc(screenY, 4096));
            prim[0].x1 = psx.conv(i16, @divTrunc(screenX +% cosStep, 4096));
            prim[0].y1 = psx.conv(i16, @divTrunc(screenY +% sinStep, 4096));
            prim[0].x2 = psx.conv(i16, @divTrunc(screenX -% sinStep, 4096));
            prim[0].y2 = psx.conv(i16, @divTrunc(screenY +% cosStep, 4096));
            prim[0].x3 = psx.conv(i16, @divTrunc((screenX +% cosStep) -% sinStep, 4096));
            prim[0].y3 = psx.conv(i16, @divTrunc((screenY +% sinStep) +% cosStep, 4096));

            prim[0].r0 = tile.colors[0].r;
            prim[0].g0 = tile.colors[0].g;
            prim[0].b0 = tile.colors[0].b;
            prim[0].r1 = tile.colors[1].r;
            prim[0].g1 = tile.colors[1].g;
            prim[0].b1 = tile.colors[1].b;
            prim[0].r2 = tile.colors[2].r;
            prim[0].g2 = tile.colors[2].g;
            prim[0].b2 = tile.colors[2].b;
            prim[0].r3 = tile.colors[3].r;
            prim[0].g3 = tile.colors[3].g;
            prim[0].b3 = tile.colors[3].b;

            prim[0].u0 = psx.conv(u8, tile.uv.x);
            prim[0].v0 = psx.conv(u8, tile.uv.y);
            prim[0].u1 = psx.conv(u8, @as(c_int, tile.uv.x) +% tile.uv.w);
            prim[0].v1 = psx.conv(u8, tile.uv.y);
            prim[0].u2 = psx.conv(u8, tile.uv.x);
            prim[0].v2 = psx.conv(u8, @as(c_int, tile.uv.y) +% tile.uv.h);
            prim[0].u3 = psx.conv(u8, @as(c_int, tile.uv.x) +% tile.uv.w);
            prim[0].v3 = psx.conv(u8, @as(c_int, tile.uv.y) +% tile.uv.h);

            prim[0].tpage = psx.conv(u16, tpage | (@as(c_int, tile.colors[1].cd) << 7));
            prim[0].clut = 0x3BB0;

            buffer += @sizeOf(v.POLY_GT4);
            c.AddPrim(psx.byteAt(u8, psx.get(scratch.unk4).?, 0x1FFC), @ptrCast(prim));
            prim += 1;
        }
    }

    const background: *align(1) v.TILE = @ptrCast(buffer);
    const bgTag: *align(1) v.P_TAG = @ptrCast(background);
    psx.setBits(bgTag, "len", 3);
    bgTag.code = 0x60;
    background.r0 = 0;
    background.g0 = 0;
    background.b0 = 0;
    background.x0 = 0;
    background.y0 = 0;
    background.w = 320;
    background.h = 240;
    c.AddPrim(psx.byteAt(u8, psx.get(scratch.unk4).?, 0x1FFC), @ptrCast(background));

    psx.put(&scratch.unk0, psx.byteAt(v.TILE, background, @sizeOf(v.TILE)));
}
