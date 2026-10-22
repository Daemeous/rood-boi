//! zig_diff spec: func_800E1764 -> func_800E1764.func_800E1764 (771A8.c).
//! Inputs: a movementDestinationState in the arena with random bytes. Its
//! referenceX/Z are drawn from +-2^30 with the extremes included, which keeps
//! `x * 128 - referenceX + 64` clear of signed overflow. Its destination x/z
//! bytes are often 0, 1, 127, 128 or 255. The angle is an edge value (0, -1,
//! 0x7FF, 0x800, -2048, -2049, 0xFFF, 0x1000) or a random value in +-2^30.
//! The callees are recording stubs. func_800E1388 sometimes rewrites only the
//! y/status bytes of the destination, so the two distances agree while the raw
//! words differ. func_800E4660 returns a hash of its arguments, so equal
//! arguments give equal distances and the unsigned compare sees both orders.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_771A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E1764));

pub const classes = [_][:0]const u8{ "angle-negative", "angle-wide", "angle-low-12" };

const ref_edges = [_]i32{ 0, 1, -1, 1 << 30, -(1 << 30) };
const angle_edges = [_]c_int{ 0, -1, 0x7FF, 0x800, -2048, -2049, 0xFFF, 0x1000 };

pub const stubs = struct {
    pub fn func_800E1388(ctx: *zd.Ctx, s: anytype, pos: anytype, angle: anytype, dist: anytype) void {
        _ = pos;
        _ = angle;
        _ = dist;
        const p = s orelse return;
        const st: *v.movementDestinationState = @ptrCast(@alignCast(p));
        switch (ctx.value(u2)) {
            0 => {},
            1, 2 => {
                st.destination._bytes[1] = ctx.value(u8);
                st.destination._bytes[3] = ctx.value(u8);
            },
            3 => {
                st.destination.raw = ctx.value(u32);
            },
        }
        if (ctx.value(u3) == 0) st.referenceX = @rem(ctx.value(i32), 1 << 30);
        if (ctx.value(u3) == 0) st.referenceZ = @rem(ctx.value(i32), 1 << 30);
    }

    pub fn func_800E4660(ctx: *zd.Ctx, a: anytype, b: anytype, d: anytype) c_uint {
        _ = ctx;
        const x: u32 = @bitCast(@as(c_int, a));
        const y: u32 = @bitCast(@as(c_int, b));
        const z: u32 = @bitCast(@as(c_int, d));
        var h = x *% 0x9E3779B1 +% y *% 0x85EBCA77 +% z *% 0xC2B2AE3D;
        h ^= h >> 15;
        return h;
    }

    pub fn func_800E153C(ctx: *zd.Ctx, s: anytype, unused: anytype) void {
        _ = unused;
        if (s) |p| {
            const st: *v.movementDestinationState = @ptrCast(@alignCast(p));
            st.flags = ctx.value(u16);
        }
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const st = ctx.alloc(v.movementDestinationState);
    st.referenceX = if (ctx.chance(3))
        ref_edges[ctx.range(usize, 0, ref_edges.len - 1)]
    else
        ctx.range(i32, -(1 << 30), 1 << 30);
    st.referenceZ = if (ctx.chance(3))
        ref_edges[ctx.range(usize, 0, ref_edges.len - 1)]
    else
        ctx.range(i32, -(1 << 30), 1 << 30);
    if (ctx.chance(3)) {
        st.destination._bytes[0] = ctx.pick(u8, &.{ 0, 1, 127, 128, 255 });
        st.destination._bytes[2] = ctx.pick(u8, &.{ 0, 1, 127, 128, 255 });
    }
    const angle: c_int = if (ctx.chance(3))
        angle_edges[ctx.range(usize, 0, angle_edges.len - 1)]
    else
        ctx.range(c_int, -(1 << 30), 1 << 30);
    return .{ st, angle };
}

pub fn classify(args: Args) usize {
    const angle = args[1];
    if (angle < 0) return 0;
    if (angle >= 0x1000) return 1;
    return 2;
}
