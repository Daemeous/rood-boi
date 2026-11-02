//! An effect point's random spline controls (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D05F4 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// The two spline control points of an effect point between its saved position and endpoint: along the line by the shape's nibbles, off it at a radius and angle with random spreads, in the line's frame (libgte).
pub fn func_800D05F4(mode_arg: ?*v.func_800D0548_arg0, point: ?*v.func_800D0548_arg1) callconv(.c) void {
    const a = mode_arg.?;
    const b = point.?;
    var control0: v.SVECTOR = undefined;
    var control1: v.SVECTOR = undefined;
    var delta: v.VECTOR = undefined;
    var start: v.VECTOR = undefined;
    var end: v.VECTOR = undefined;
    var transform: v.MATRIX = undefined;
    var distance: c_int = undefined;
    if ((a.mode & 0x80) != 0) {
        start.vx = b.saved.vx;
        start.vy = b.saved.vy;
        start.vz = b.saved.vz;
        end.vx = b.endpoint.vx;
        end.vy = b.endpoint.vy;
        end.vz = b.endpoint.vz;
    } else {
        start.vx = b.endpoint.vx;
        start.vy = b.endpoint.vy;
        start.vz = b.endpoint.vz;
        end.vx = b.saved.vx;
        end.vy = b.saved.vy;
        end.vz = b.saved.vz;
    }
    delta.vx = end.vx -% start.vx;
    delta.vy = end.vy -% start.vy;
    delta.vz = end.vz -% start.vz;
    distance = c.vs_gte_rsqrt(delta.vx *% delta.vx +% delta.vy *% delta.vy +% delta.vz *% delta.vz);
    const shape: c_int = a.shape12;
    control0.vz = psx.conv(i16, @divTrunc(distance *% (((shape >> 8) & 15) - 8), 8));
    control1.vz = psx.conv(i16, distance +% @divTrunc(distance *% ((shape >> 12) - 8), 8));
    const limit: c_int = @as(*align(1) u16, @ptrCast(&a.limit)).*; // *(u_short*)&arg0->limit: limit and the byte after it
    var radius1: c_int = (limit >> 3) & 0x1E0;
    var radius0: c_int = radius1;
    if ((limit & 0xF000) != 0) {
        radius0 = radius1 +% @rem(c.rand(), (limit >> 12) << 5);
        radius1 +%= @rem(c.rand(), (limit >> 12) << 5);
    }
    var angle1: c_int = (@as(c_int, psx.conv(u8, a.shape12)) & 15) << 8;
    var angle0: c_int = angle1;
    if ((shape & 0xF0) != 0) {
        angle0 = angle1 +% @rem(c.rand(), (shape * 16) & 0xF00); // shape < 0x10000: no overflow
        angle1 +%= @rem(c.rand(), (shape * 16) & 0xF00);
    }
    control0.vx = psx.conv(i16, (c.rcos(angle0) *% radius0) >> 12);
    control0.vy = psx.conv(i16, (c.rsin(angle0) *% radius0) >> 12);
    control1.vx = psx.conv(i16, (c.rcos(angle1) *% radius1) >> 12);
    control1.vy = psx.conv(i16, (c.rsin(angle1) *% radius1) >> 12);
    c.vs_battle_lookAt(@ptrCast(&start), @ptrCast(&end), @ptrCast(&transform));
    transform.t[0] = start.vx;
    transform.t[1] = start.vy;
    transform.t[2] = start.vz;
    c.SetRotMatrix(@ptrCast(&transform));
    c.SetTransMatrix(@ptrCast(&transform));
    c.RotTrans(@ptrCast(&control0), @ptrCast(&start), &distance);
    c.RotTrans(@ptrCast(&control1), @ptrCast(&end), &distance);
    b.control0.vx = psx.conv(i16, start.vx);
    b.control0.vy = psx.conv(i16, start.vy);
    b.control0.vz = psx.conv(i16, start.vz);
    b.control1.vx = psx.conv(i16, end.vx);
    b.control1.vy = psx.conv(i16, end.vy);
    b.control1.vz = psx.conv(i16, end.vz);
}
