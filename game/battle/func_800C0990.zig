//! Draws one line primitive (G-style line with a fixed colour) into the
//! ordering table at its depth (pc/src/game/BATTLE/BATTLE.PRG/573B8.c).
const common = @import("common");
const psx = common.psx;
const sprite_item = common.sprite_item;
const v = @import("views_573B8");

/// orig: func_800C0990 (pc/src/game/BATTLE/BATTLE.PRG/573B8.c)
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
            angle = @as(c_int, v.g.vs_core_mathArcTangentTable()[@as(usize, @intCast(angle))]);
            angle = shift -% angle;
        } else {
            angle = ay << 9;
            angle = @divTrunc(angle, ax);
            angle = @as(c_int, v.g.vs_core_mathArcTangentTable()[@as(usize, @intCast(angle))]);
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

    // A flat line (the code byte of `color`: 0x40, or 0x42 for a semi-transparent one) in the default draw mode, `intensity` scaling
    // the packed colour as the C does (the product's overflow into the code byte is the game's).
    const ot_base: [*]align(1) u32 = @ptrCast(psx.get(v.g.vs_scratch().unk4).?);
    const ot = ot_base + @as(usize, @intCast(depth));
    const word: u32 = ((((color << 8) >> 8) *% psx.conv(c_uint, intensity)) | ((color >> 24) << 24));
    const start_xy: c_int = @bitCast((psx.conv(c_uint, ay) & 0xFFFF) | (psx.conv(c_uint, ax) << 16));
    _ = sprite_item.lineMode(ot, (word >> 24) & 2 != 0, @bitCast(word & 0xFFFFFF), start_xy, @as(*align(1) c_int, @ptrCast(e)).*, 0x200);
}
