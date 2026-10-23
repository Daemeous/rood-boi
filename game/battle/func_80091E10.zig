//! Scrolls a textured quad's UV window across the room geometry section (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");

/// orig: func_80091E10 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Advances a scrolling UV window every `delay` ticks and rewrites the quad's texture coordinates in the room geometry.
pub fn func_80091E10(arg0: ?*v.func_80091E10_t) callconv(.c) c_int {
    const quad = arg0.?;
    // psx_raw of the geometry section: the 4-byte guest address, widened as a host address.
    const base: usize = psx.rawOf(v.g.vs_battle_roomData().geometrySection);

    // ++tick is a u8 increment: the stored (wrapped) value is what the comparison sees.
    quad.tick +%= 1;
    if (@as(c_int, quad.tick) > @as(c_int, quad.delay)) {
        const baseU: c_int = quad.baseU;
        const baseV: c_int = quad.baseV;
        const width: c_int = quad.width;
        const height: c_int = quad.height;
        const windowWidth: c_int = quad.windowWidth;
        const windowHeight: c_int = quad.windowHeight;

        var uPos: c_int = @as(c_int, quad.currentU) +% @as(c_int, quad.stepU);
        var vPos: c_int = @as(c_int, quad.currentV) +% @as(c_int, quad.stepV);
        quad.tick = 0;

        if (uPos < baseU) {
            uPos = baseU +% width -% windowWidth;
        }
        if (vPos < baseV) {
            vPos = baseV +% height -% windowHeight;
        }
        if (baseU +% width < uPos +% windowWidth) {
            uPos = baseU;
        }
        if (baseV +% height < vPos +% windowHeight) {
            vPos = baseV;
        }

        quad.currentU = psx.conv(u8, uPos);
        quad.currentV = psx.conv(u8, vPos);

        // base + geometryOffset: a signed offset, pointer arithmetic on the guest address.
        const polygon: [*]align(1) u8 = @ptrFromInt(base +% @as(usize, @bitCast(@as(isize, quad.geometryOffset))));
        if ((@as(c_int, polygon[0xF]) & 0x3C) == 0x34) {
            polygon[0x18] = psx.conv(u8, @as(c_int, quad.u[0]) +% uPos);
            polygon[0x1C] = psx.conv(u8, @as(c_int, quad.u[1]) +% uPos);
            polygon[0x13] = psx.conv(u8, @as(c_int, quad.u[2]) +% uPos);
            polygon[0x19] = psx.conv(u8, @as(c_int, quad.v[0]) +% vPos);
            polygon[0x1D] = psx.conv(u8, @as(c_int, quad.v[1]) +% vPos);
            polygon[0x17] = psx.conv(u8, @as(c_int, quad.v[2]) +% vPos);
        } else {
            polygon[0x18] = psx.conv(u8, @as(c_int, quad.u[0]) +% uPos);
            polygon[0x1C] = psx.conv(u8, @as(c_int, quad.u[1]) +% uPos);
            polygon[0x13] = psx.conv(u8, @as(c_int, quad.u[2]) +% uPos);
            polygon[0x23] = psx.conv(u8, @as(c_int, quad.u[3]) +% uPos);
            polygon[0x19] = psx.conv(u8, @as(c_int, quad.v[0]) +% vPos);
            polygon[0x1D] = psx.conv(u8, @as(c_int, quad.v[1]) +% vPos);
            polygon[0x17] = psx.conv(u8, @as(c_int, quad.v[2]) +% vPos);
            polygon[0x27] = psx.conv(u8, @as(c_int, quad.v[3]) +% vPos);
        }
    }
    return 1;
}
