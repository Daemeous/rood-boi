//! Draws one line primitive (G-style line with a fixed colour) into the
//! ordering table at its depth (src/BATTLE/BATTLE.PRG/573B8.c).
const psx = @import("common").psx;
const v = @import("views_573B8");

/// orig: func_800C0990 (src/BATTLE/BATTLE.PRG/573B8.c)
/// Links a shaded line primitive between two SVECTORs into the scratch ordering table at their depth.
pub fn func_800C0990(start: ?*v.SVECTOR, end: ?*v.SVECTOR, color: c_uint, intensity_in: c_int) callconv(.c) void {
    const s = start.?;
    const e = end.?;
    const depth: c_int = (@as(c_int, s.vz) + @as(c_int, e.vz)) >> 1;
    if (psx.conv(c_uint, depth) >= 2048) return;

    const dy: c_int = @as(c_int, e.vy) - @as(c_int, s.vy);
    const dx: c_int = @as(c_int, e.vx) - @as(c_int, s.vx);
    var angle: c_int = 0;
    var shift: c_int = dy | dx;
    var ax: c_int = undefined;
    var ay: c_int = undefined;
    if (shift != 0) {
        ax = dx;
        ay = dy;
        if (ax < 0) ax = -%ax;
        if (ay < 0) ay = -%ay;
        shift = 512;
        if (ax < ay) {
            angle = ax << 9;
            angle = @divTrunc(angle, ay);
            angle = @as(c_int, v.g.D_80040A14()[@as(usize, @intCast(angle))]);
            angle = shift -% angle;
        } else {
            angle = ay << 9;
            angle = @divTrunc(angle, ax);
            angle = @as(c_int, v.g.D_80040A14()[@as(usize, @intCast(angle))]);
        }
        shift = 1024;
        if (ax != dx) angle = shift -% angle;
        angle *%= 2;
        if (ay != dy) angle = -%angle;
    }
    shift = 30 -% (((angle +% 256) >> 8) & 14);
    const sh: u5 = @intCast(shift);
    ay = @as(c_int, s.vx) +% ((@as(c_int, 0x4FC5) << sh) >> 30);
    ax = @as(c_int, s.vy) +% ((@as(c_int, 0xFC54) << sh) >> 30);

    var intensity: c_int = intensity_in;
    if (intensity < 0) {
        intensity = 0;
    } else if (intensity >= 64) {
        intensity = 63;
    }

    const prim: [*]align(1) u32 = @ptrCast(psx.get(v.g.vs_scratch().unk0).?);
    const ot_base: [*]align(1) u32 = @ptrCast(psx.get(v.g.vs_scratch().unk4).?);
    const ot = ot_base + @as(usize, @intCast(depth));
    var link: u32 = ot[0];
    link &= 0xFFFFFF;
    prim[1] = 0xE1000200;
    prim[3] = (psx.conv(c_uint, ay) & 0xFFFF) | (psx.conv(c_uint, ax) << 16);
    prim[0] = link | 0x04000000;
    prim[2] = ((((color << 8) >> 8) *% psx.conv(c_uint, intensity)) | ((color >> 24) << 24));
    prim[4] = @as(*align(1) u32, @ptrCast(e)).*;
    ot[0] = (psx.addr(prim) << 8) >> 8;
    psx.put(&v.g.vs_scratch().unk0, @as(*anyopaque, @ptrCast(prim + 5)));
}
