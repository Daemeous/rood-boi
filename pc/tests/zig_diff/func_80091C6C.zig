//! zig_diff spec: func_80091C6C -> func_80091C6C.func_80091C6C (2842C.c).
//! Inputs: a quad block in the arena (tick and delay, frame and frameCount,
//! columns 1..255 so the modulo and division never fault, tile bases and
//! sizes, u and v arrays) and a room whose geometry section points at a fixed
//! arena area. geometryOffset is chosen inside that area, and each polygon's
//! byte 0xF matches the 0x34 test about half the time, so both the triangle
//! and the quad paths run. tick+1 == delay and frame+1 == frameCount come up
//! often, covering the equal sides of the two ordered tests. Byte wrap
//! (tick and frame at 255) and u/v sums past 255 are included.

const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80091C6C));

/// Index 0: the tick does not pass delay. 1..4: it does, and the frame wraps
/// (1, 2) or not (3, 4), on a triangle (1, 3) or a quad (2, 4) polygon.
pub const classes = [_][:0]const u8{ "no-advance", "wrap-tri", "wrap-quad", "advance-tri", "advance-quad" };

/// Where the geometry section starts in the arena. Offsets are drawn so that
/// every polygon byte the C writes (up to +0x27) stays inside the arena.
const geo_at: usize = 0x10000;

fn geoPtr() *i32 {
    return @ptrCast(@alignCast(&zd.zdiff_arena[geo_at]));
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const quad = ctx.alloc(v.func_80091C6C_t);

    psx.put(&v.g.vs_battle_roomData().geometrySection, geoPtr());

    quad.geometryOffset = if (ctx.chance(3))
        ctx.edge(i32, &.{ -0x8000, -1, 0, 1, 0x7F00 })
    else
        ctx.range(i32, -0x8000, 0x7F00);

    // Byte 0xF of the polygon: the 0x34 test matches about half the time.
    const poly: usize = @intCast(@as(isize, geo_at) + @as(isize, quad.geometryOffset));
    const sel = &zd.zdiff_arena[poly + 0xF];
    if (ctx.chance(2)) sel.* = (sel.* & 0xC3) | 0x34;

    quad.tick = ctx.pick(u8, &.{ 0, 254, 255 });
    quad.delay = if (ctx.chance(3)) quad.tick +% 1 else ctx.pick(u8, &.{ 0, 1, 255 });

    quad.frame = ctx.pick(u8, &.{ 0, 254, 255 });
    quad.frameCount = if (ctx.chance(3)) quad.frame +% 1 else ctx.pick(u8, &.{ 0, 1, 2, 255 });

    // columns is a divisor: never 0.
    quad.columns = if (ctx.chance(2)) ctx.edge(u8, &.{ 1, 2, 3, 255 }) else ctx.range(u8, 1, 255);

    quad.baseU = ctx.pick(u8, &.{ 0, 255 });
    quad.baseV = ctx.pick(u8, &.{ 0, 255 });
    quad.frameWidth = ctx.pick(u8, &.{ 0, 1, 255 });
    quad.frameHeight = ctx.pick(u8, &.{ 0, 1, 255 });

    for (&quad.u) |*e| e.* = ctx.pick(u8, &.{ 0, 255 });
    for (&quad.v) |*e| e.* = ctx.pick(u8, &.{ 0, 255 });

    return .{quad};
}

pub fn classify(args: Args) usize {
    const q = args[0].?;
    const t: u8 = q.tick +% 1;
    if (t <= q.delay) return 0;
    const wrap = (q.frame +% 1) >= q.frameCount;
    const poly: usize = @intCast(@as(isize, geo_at) + @as(isize, q.geometryOffset));
    const tri = (zd.zdiff_arena[poly + 0xF] & 0x3C) == 0x34;
    const kind: usize = if (wrap) 1 else 3;
    const quad_extra: usize = if (tri) 0 else 1;
    return kind + quad_extra;
}
