//! zig_diff spec: _computeStepAndDisplacement -> compute_step_and_displacement.computeStepAndDisplacement (146C.c).
//! Inputs: an arena target point (edges and random), clamped or not;
//! RotTransPers' stub projects it to a screen point chosen around the
//! box's edges (x 60/61/259/260, y 60/61/179/180, negative too); the
//! clamp stub folds each coordinate into +-0x100000; VectorNormal's stub
//! picks the direction from the displacement already written, so the
//! distance lands on 39, 40 and 41 and each axis may be 0; the camera
//! speed and maxSpeed around their bounds. The stubs record the vectors
//! they are given (stack locals the trace only tags), RotTransPers' point
//! padding included.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").computeStepAndDisplacement));

pub const classes = [_][:0]const u8{ "centred", "far", "near" };

var screen: c_int = 0;
var aim: u8 = 0;
var zero: u8 = 0;
var displacement: ?*v.VECTOR = null;

fn toward(d: i32, k: i32) i32 {
    const q = @divTrunc(d, k);
    return if (q == 0) 1 else q;
}

pub const stubs = struct {
    pub fn _clampPositionToZoneBounds(_: *zd.Ctx, in: anytype, out: anytype) void {
        const x = in[0].vx;
        const y = in[0].vy;
        const z = in[0].vz;
        out[0].vx = std.math.clamp(x, -0x100801, 0x100801);
        out[0].vy = std.math.clamp(y, -0x100801, 0x100801);
        out[0].vz = std.math.clamp(z, -0x100801, 0x100801);
    }
    pub fn RotTransPers(ctx: *zd.Ctx, point: anytype, sxy: anytype, p: anytype, flag: anytype) c_int {
        const seen = ctx.alloc(v.SVECTOR);
        seen.vx = point[0].vx;
        seen.vy = point[0].vy;
        seen.vz = point[0].vz;
        // pad reaches the GTE's VZ0 upper half: it must be defined (0xAA from Zig's undefined diverges from the C stack).
        seen.pad = point[0].pad;
        sxy[0] = screen;
        p[0] = ctx.value(c_int);
        flag[0] = ctx.value(c_int);
        return 0;
    }
    pub fn VectorNormal(ctx: *zd.Ctx, in: anytype, out: anytype) c_int {
        const seen = ctx.alloc(v.VECTOR);
        seen.vx = in[0].vx;
        seen.vy = in[0].vy;
        seen.vz = in[0].vz;
        const d = displacement.?;
        const k: i32 = switch (aim) {
            0 => 40,
            1 => 41,
            2 => 39,
            else => 1000,
        };
        out[0].vx = if (zero & 1 != 0) 0 else toward(d.vx, k);
        out[0].vz = if (zero & 2 != 0) 0 else toward(d.vz, k);
        out[0].vy = if (zero & 4 != 0) 0 else toward(d.vy, k);
        return 0;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const target = ctx.alloc(v.SVECTOR);
    target.vx = ctx.pick(i16, &.{ 0, 100, -100, 0x7FFF, -0x8000 });
    target.vy = ctx.pick(i16, &.{ 0, 100, -100, 0x7FFF, -0x8000 });
    target.vz = ctx.pick(i16, &.{ 0, 100, -100, 0x7FFF, -0x8000 });
    const look = &v.g.vs_scratch().camera.lookAt;
    look.vx = ctx.pick(i32, &.{ 0, 0x10000, -0x10000 });
    look.vy = ctx.pick(i32, &.{ 0, 0x10000, -0x10000 });
    look.vz = ctx.pick(i32, &.{ 0, 0x10000, -0x10000 });
    const x = ctx.edge(c_int, &.{ 60, 61, 160, 259, 260, -1 });
    const y = ctx.edge(c_int, &.{ 60, 61, 120, 179, 180, -1 });
    screen = x +% y *% 0x10000;
    aim = ctx.range(u8, 0, 3);
    zero = if (ctx.chance(2)) 0 else ctx.range(u8, 0, 7);
    const out = ctx.alloc(v.VECTOR);
    displacement = out;
    const maxSpeed = ctx.pick(c_int, &.{ 8, 16, 64, 0 });
    v.g.vs_battle_cameraCurrentSpherical().speed = ctx.edge(c_int, &.{ 0, 7, 8, 15, 16, maxSpeed - 8, maxSpeed - 9, maxSpeed - 7, 100 });
    return .{ target, ctx.alloc(v.VECTOR), out, if (ctx.chance(2)) 0 else ctx.pick(c_int, &.{ 1, -1 }), maxSpeed };
}

pub fn classify(args: Args) usize {
    if (args[3] == 0) {
        const lo = screen -% @divTrunc(screen, 0x10000) *% 0x10000;
        const hi = @divTrunc(screen, 0x10000);
        if (@as(u32, @bitCast(lo -% 61)) < 199 and hi > 60 and hi < 180) return 0;
    }
    return if (aim == 1 or aim == 3) 2 else 1;
}
