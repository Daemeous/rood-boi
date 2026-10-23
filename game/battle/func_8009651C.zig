//! Draws the shadow quad under a unit: a textured quad around its position,
//! one of eight corners picked by facing (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_8009651C (src/BATTLE/BATTLE.PRG/2842C.c)
/// Builds the shadow quad at `position` for `direction` and links it into the ordering table.
pub fn func_8009651C(position: ?*v.SVECTOR, direction: c_int) callconv(.c) void {
    const dx = [8]c_int{ -8, 0, 7, 10, 7, 0, -8, -11 };
    const dy = [8]c_int{ -8, -11, -8, 0, 7, 10, 7, 0 };
    // The C reads the position before the direction test, so a null position faults here too.
    const x: c_int = position.?.vx;
    const y: c_int = position.?.vy;
    const scratch: *v.vs_scratch_t = @ptrFromInt(0x1F800000);

    var index: usize = switch (direction) {
        4 => 0,
        6 => 1,
        2 => 2,
        10 => 3,
        8 => 4,
        9 => 5,
        1 => 6,
        5 => 7,
        else => return,
    };

    const prim: *align(1) v.POLY_FT4 = @ptrCast(psx.get(scratch.unk0).?);
    const tag: *align(1) v.P_TAG = @ptrCast(prim);
    // setPolyFT4: len = 9, code = 0x2c; setShadeTex(prim, 1) then sets bit 0 of code.
    psx.setBits(tag, "len", 9);
    prim.code = 0x2c;
    prim.code |= 0x01;

    prim.x0 = psx.conv(i16, dx[index] +% x);
    prim.y0 = psx.conv(i16, dy[index] +% y);
    index = (index + 2) & 7;
    prim.x1 = psx.conv(i16, dx[index] +% x);
    prim.y1 = psx.conv(i16, dy[index] +% y);
    index = (index + 2) & 7;
    prim.x3 = psx.conv(i16, dx[index] +% x);
    prim.y3 = psx.conv(i16, dy[index] +% y);
    index = (index + 2) & 7;
    prim.x2 = psx.conv(i16, dx[index] +% x);
    prim.y2 = psx.conv(i16, dy[index] +% y);

    // setUV4(prim, 48, 48, 63, 48, 48, 63, 63, 63)
    prim.u0 = 48;
    prim.v0 = 48;
    prim.u1 = 63;
    prim.v1 = 48;
    prim.u2 = 48;
    prim.v2 = 63;
    prim.u3 = 63;
    prim.v3 = 63;
    // setTPage(prim, 0, 0, 768, 0): (768 & 0x3ff) >> 6
    prim.tpage = (768 & 0x3ff) >> 6;
    // setClut(prim, 976, 223)
    prim.clut = (223 << 6) | ((976 >> 4) & 0x3f);

    // AddPrim(psx_get(unk4) - 16, prim++): the ordering table address is a guest address, 16 bytes back.
    const ot_addr: u32 = psx.addr(psx.get(scratch.unk4).?) -% 16;
    c.AddPrim(@as(*anyopaque, @ptrFromInt(@as(usize, ot_addr))), prim);

    // prim++ (28 bytes), then psx_set(unk0, prim).
    const next: *anyopaque = psx.byteAt(v.POLY_FT4, prim, @sizeOf(v.POLY_FT4));
    psx.put(&scratch.unk0, next);
}
