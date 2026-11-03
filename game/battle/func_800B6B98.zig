//! A text box's screen placement beside its origin, clamped to the screen (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B6B98 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Places a script[6] by script[7] character box at the origin plus func_800CCE10's offset (by
/// script[5]'s low nibble and the origin's vz ^ direction), clamped inside (4, 4)..(320, 216);
/// returns 1 when it fits, else 0 with the clamp's distance (vs_gte_rsqrt of the shift).
pub fn func_800B6B98(origin: ?*v.SVECTOR, script: [*c]u8, result: ?*v.textPlacement, direction: u8) callconv(.c) c_int {
    const o = origin.?;
    const r = result.?;
    var dx: i16 = 0;
    var dy: i16 = 0;
    const offset: c.textOffset = c.func_800CCE10(script[5] & 0xF, (@as(c_int, o.vz) ^ direction) *% 4, script[6], script[7]);
    r.offset = .{ ._bits_0 = @bitCast(offset) };
    const x: i16 = psx.conv(i16, @as(c_int, o.vx) +% psx.getBits(&r.offset, "x"));
    r.x = x;
    const y: i16 = psx.conv(i16, @as(c_int, o.vy) +% psx.getBits(&r.offset, "y"));
    r.y = y;
    var height: i16 = psx.conv(i16, @as(c_int, script[7]) *% 13 +% 4);
    var width: i16 = psx.conv(i16, @as(c_int, script[6]) *% 12 +% 8);

    if (x < 4) {
        r.x = 4;
        dx = psx.conv(i16, 4 -% @as(c_int, x));
    } else if (@as(c_int, x) +% width > 320) {
        width = psx.conv(i16, 320 -% @as(c_int, width));
        r.x = width;
        dx = psx.conv(i16, @as(c_int, width) -% x);
    }

    if (y < 4) {
        r.y = 4;
        dy = psx.conv(i16, 4 -% @as(c_int, y));
    } else if (@as(c_int, height) +% y > 216) {
        height = psx.conv(i16, 216 -% @as(c_int, height));
        r.y = height;
        dy = psx.conv(i16, @as(c_int, height) -% y);
    }

    if (dx == 0 and dy == 0) return 1;
    r.distance = c.vs_gte_rsqrt(@as(c_int, dx) *% dx +% @as(c_int, dy) *% dy);
    return 0;
}
