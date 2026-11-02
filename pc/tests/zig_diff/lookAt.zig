//! zig_diff spec: vs_battle_lookAt -> look_at.lookAt (146C.c).
//! Inputs: two arena points (random, and at the 32-bit edges so the
//! difference wraps) and an arena matrix. The libgte stubs are
//! deterministic stand-ins on the vectors' x, y, z (the C's locals live on
//! the stack, which the trace only tags): VectorNormal scales and offsets
//! each component, OuterProduct0 is the wrapping cross product; both read
//! their inputs before writing (the C normalises in place).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").lookAt));

pub const classes = [_][:0]const u8{"any"};

pub const stubs = struct {
    pub fn VectorNormal(_: *zd.Ctx, in: anytype, out: anytype) c_int {
        const x = in[0].vx;
        const y = in[0].vy;
        const z = in[0].vz;
        out[0].vx = (x >> 3) +% 1;
        out[0].vy = (y >> 4) -% 2;
        out[0].vz = (z >> 5) ^ 0x55;
        return 0;
    }
    pub fn OuterProduct0(_: *zd.Ctx, a: anytype, b: anytype, out: anytype) void {
        const ax = a[0].vx;
        const ay = a[0].vy;
        const az = a[0].vz;
        const bx = b[0].vx;
        const by = b[0].vy;
        const bz = b[0].vz;
        out[0].vx = ay *% bz -% az *% by;
        out[0].vy = az *% bx -% ax *% bz;
        out[0].vz = ax *% by -% ay *% bx;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const from = ctx.alloc(v.VECTOR);
    const to = ctx.alloc(v.VECTOR);
    from.vx = ctx.pick(i32, &.{ 0, 4096, -0x8000_0000 });
    to.vx = ctx.pick(i32, &.{ 0, 4096, 0x7FFF_FFFF });
    return .{ from, to, ctx.alloc(v.MATRIX) };
}

pub fn classify(_: Args) usize {
    return 0;
}
