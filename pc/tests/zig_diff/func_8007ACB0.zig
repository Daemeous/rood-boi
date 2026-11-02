//! zig_diff spec: func_8007ACB0 -> func_8007ACB0.func_8007ACB0 (146C.c).
//! Inputs: the camera's position and look-at (equal in x and z sometimes:
//! the forward fallback), angles and far clip random and at edges; the
//! editor hook (pc_live_camera) moves both points half the time. The
//! libgte stubs are deterministic stand-ins on the components the game
//! reads (the C's vectors and roll matrix live on the stack, which the
//! trace only tags): VectorNormal scales and offsets, OuterProduct0 is the
//! wrapping cross product, ScaleMatrix and ApplyMatrix their fixed-point
//! forms, func_80041C68 mixes the roll into the view, rcos/rsin are linear.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8007ACB0));

pub const classes = [_][:0]const u8{ "fallback", "aimed" };

var live = false;

pub const stubs = struct {
    pub fn pc_live_camera(_: *zd.Ctx, position: anytype, lookAt: anytype) c_int {
        if (live) {
            position[0].vx +%= 0x12345;
            lookAt[0].vz -%= 0x6789;
        }
        return 0;
    }
    pub fn VectorNormal(_: *zd.Ctx, in: anytype, out: anytype) c_int {
        const x = in[0].vx;
        const y = in[0].vy;
        const z = in[0].vz;
        out[0].vx = x *% 3 +% 1;
        out[0].vy = y *% 5 -% 2;
        out[0].vz = z *% 7 +% 0x10000;
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
    pub fn rcos(_: *zd.Ctx, a: anytype) c_int {
        return a *% 3 +% 1;
    }
    pub fn rsin(_: *zd.Ctx, a: anytype) c_int {
        return a *% 5 -% 7;
    }
    pub fn ScaleMatrix(_: *zd.Ctx, m: anytype, s: anytype) @TypeOf(m) {
        const scale = [3]i32{ s[0].vx, s[0].vy, s[0].vz };
        for (0..3) |i| {
            for (0..3) |j| m[0].m[i][j] = @truncate((@as(i32, m[0].m[i][j]) *% scale[j]) >> 12);
        }
        return m;
    }
    pub fn func_80041C68(_: *zd.Ctx, roll: anytype, view: anytype) void {
        for (0..3) |i| {
            for (0..3) |j| view[0].m[i][j] = view[0].m[i][j] +% roll[0].m[j][i];
        }
    }
    pub fn ApplyMatrix(_: *zd.Ctx, m: anytype, sv: anytype, out: anytype) @TypeOf(out) {
        const x: i32 = sv[0].vx;
        const y: i32 = sv[0].vy;
        const z: i32 = sv[0].vz;
        for (0..3) |i| {
            const r = (@as(i32, m[0].m[i][0]) *% x +% @as(i32, m[0].m[i][1]) *% y +% @as(i32, m[0].m[i][2]) *% z) >> 12;
            switch (i) {
                0 => out[0].vx = r,
                1 => out[0].vy = r,
                else => out[0].vz = r,
            }
        }
        return out;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const camera = &v.g.vs_scratch().camera;
    live = ctx.chance(2);
    camera.position.vx = ctx.pick(i32, &.{ 0, 4096, -4096, 0x7FFF_FFFF });
    camera.position.vz = ctx.pick(i32, &.{ 0, 4096, -4096 });
    if (ctx.chance(3)) {
        camera.lookAt.vx = camera.position.vx +% ctx.edge(i32, &.{ 0, 4095, -4095 });
        camera.lookAt.vz = camera.position.vz +% ctx.edge(i32, &.{ 0, 4095, -4095 });
        live = false;
    }
    camera.farClip = ctx.pick(i32, &.{ 0, 4096, 0x2000, -1 });
    return .{};
}

pub fn classify(_: Args) usize {
    const camera = &v.g.vs_scratch().camera;
    const dx = @divTrunc(camera.position.vx -% camera.lookAt.vx, 4096);
    const dz = @divTrunc(camera.lookAt.vz -% camera.position.vz, 4096);
    return if (!live and (dx | dz) == 0) 0 else 1;
}
