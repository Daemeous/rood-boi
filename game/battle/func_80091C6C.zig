//! Advances an animated texture's tick and frame, and rewrites the texture
//! coordinates of its polygon in the room geometry (src/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");

/// orig: func_80091C6C (src/BATTLE/BATTLE.PRG/2842C.c)
/// Steps an animated quad's tick and frame and writes its frame's UVs into the room geometry.
pub fn func_80091C6C(arg0: ?*v.func_80091C6C_t) callconv(.c) c_int {
    const anim = arg0.?;
    const base = psx.get(v.g.vs_battle_roomData().geometrySection).?;

    anim.tick +%= 1;
    if (@as(c_int, anim.tick) > @as(c_int, anim.delay)) {
        anim.tick = 0;
        anim.frame +%= 1;
        if (@as(c_int, anim.frame) >= @as(c_int, anim.frameCount)) {
            anim.frame = 0;
        }

        // The column and row division is unsigned, as the C's (u_int) cast makes it.
        const columns: c_uint = anim.columns;
        const frame: c_uint = anim.frame;
        const u: c_int = @bitCast(@as(c_uint, anim.baseU) +% @as(c_uint, anim.frameWidth) *% (frame % columns));
        const w: c_int = @bitCast(@as(c_uint, anim.baseV) +% @as(c_uint, anim.frameHeight) *% (frame / columns));

        const off: isize = anim.geometryOffset;
        if ((psx.byteAt(u8, base, off + 0xF).* & 0x3C) == 0x34) {
            psx.byteAt(u8, base, off + 0x18).* = psx.conv(u8, @as(c_int, anim.u[0]) +% u);
            psx.byteAt(u8, base, off + 0x1C).* = psx.conv(u8, @as(c_int, anim.u[1]) +% u);
            psx.byteAt(u8, base, off + 0x13).* = psx.conv(u8, @as(c_int, anim.u[2]) +% u);
            psx.byteAt(u8, base, off + 0x19).* = psx.conv(u8, @as(c_int, anim.v[0]) +% w);
            psx.byteAt(u8, base, off + 0x1D).* = psx.conv(u8, @as(c_int, anim.v[1]) +% w);
            psx.byteAt(u8, base, off + 0x17).* = psx.conv(u8, @as(c_int, anim.v[2]) +% w);
        } else {
            psx.byteAt(u8, base, off + 0x18).* = psx.conv(u8, @as(c_int, anim.u[0]) +% u);
            psx.byteAt(u8, base, off + 0x1C).* = psx.conv(u8, @as(c_int, anim.u[1]) +% u);
            psx.byteAt(u8, base, off + 0x13).* = psx.conv(u8, @as(c_int, anim.u[2]) +% u);
            psx.byteAt(u8, base, off + 0x23).* = psx.conv(u8, @as(c_int, anim.u[3]) +% u);
            psx.byteAt(u8, base, off + 0x19).* = psx.conv(u8, @as(c_int, anim.v[0]) +% w);
            psx.byteAt(u8, base, off + 0x1D).* = psx.conv(u8, @as(c_int, anim.v[1]) +% w);
            psx.byteAt(u8, base, off + 0x17).* = psx.conv(u8, @as(c_int, anim.v[2]) +% w);
            psx.byteAt(u8, base, off + 0x27).* = psx.conv(u8, @as(c_int, anim.v[3]) +% w);
        }
    }
    return 1;
}
