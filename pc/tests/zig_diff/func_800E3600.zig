//! zig_diff spec: func_800E3600 -> func_800E3600.func_800E3600 (771A8.c).
//! Inputs: a movementApproachState in the arena (the approach pointer is never
//! null: the C does not test it), with bit 0 of path set or clear, a tile and
//! destination that often share a coordinate (the ABS test at 0) or sit at the
//! radius distance (the clamp at equality), timers at the 0, 1, 127 and 0xFFFF
//! edges, unk4 zero or not, and an angle with low bits zero or not (512 exactly,
//! sign bit set or clear). mode is 0..3 so every mode table entry is valid. The
//! callees are stubs: the test functions and table entries return zero and
//! nonzero, rcos returns 3959 exactly sometimes, and func_800D8400 writes the
//! timer the C reads back.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_771A8");
const c = @import("c_771A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E3600));

pub const classes = [_][:0]const u8{
    "path-follow",
    "straight",
    "sector-idle",
    "sector-busy-mode-0-2",
    "sector-busy-mode-1-3",
};

fn near(base: u8, d: i32) u8 {
    return @intCast(std.math.clamp(@as(i32, base) + d, 0, 255));
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const st = ctx.alloc(v.movementApproachState);

    // Every entry of the mode tables is a valid callee.
    for (v.g.D_800F1760()) |*row| {
        for (row) |*e| {
            e.* = @as(?*const anyopaque, @ptrCast(&c.D_800D9B6C));
        }
    }

    st.tile.raw = ctx.int(u32);
    st.destination.raw = ctx.int(u32);

    // Destination near the tile: often equal on an axis, often at radius distance.
    const tx = st.tile._bytes[0];
    const tz = st.tile._bytes[2];
    const ox: i32 = if (ctx.chance(4)) 0 else ctx.range(i32, -40, 40);
    const oz: i32 = if (ctx.chance(4)) 0 else ctx.range(i32, -40, 40);
    st.destination._bytes[0] = near(tx, ox);
    st.destination._bytes[2] = near(tz, oz);
    const dxAct: i32 = @as(i32, st.destination._bytes[0]) - @as(i32, tx);
    const dzAct: i32 = @as(i32, st.destination._bytes[2]) - @as(i32, tz);
    const axis = if (ctx.chance(2)) dxAct else dzAct;
    st.radius = if (ctx.chance(2)) @as(u8, @intCast(@abs(axis))) else ctx.int(u8);

    st.timer = ctx.pick(u16, &.{ 0, 1, 2, 126, 127, 128, 0xFFFF });
    st.unk4 = if (ctx.chance(3)) 0 else ctx.int(u8);

    st.path = ctx.int(u32) & ~@as(u32, 1);
    if (ctx.chance(3)) st.path |= 1;

    var angle: c_int = ctx.pick(c_int, &.{ 0, 512, 513, 0x3FF, 0x400, 0x200, -1, 0x7FFF_FFFF, -0x8000_0000 });
    if (ctx.chance(3)) angle &= ~@as(c_int, 0x3FF);

    const mode: c_int = @as(c_int, ctx.range(u8, 0, 3));
    const approach: *v.func_800E0850_t = @ptrCast(@alignCast(st));
    return .{ approach, angle, mode };
}

pub fn classify(args: Args) usize {
    const st: *v.movementApproachState = @ptrCast(@alignCast(args[0].?));
    if ((st.path & 1) != 0) return 0;
    const angle: u32 = @bitCast(args[1]);
    if ((angle & 0x3FF) == 0) return 1;
    if ((st.timer -% 1) >= 126) return 2;
    return if (args[2] == 0 or args[2] == 2) 3 else 4;
}

fn stateOf(p: anytype) *v.movementApproachState {
    const q = if (@typeInfo(@TypeOf(p)) == .optional) p.? else p;
    return @ptrCast(@alignCast(q));
}

pub const stubs = struct {
    pub fn func_800E0678(ctx: *zd.Ctx, s: anytype, dir: anytype) c_int {
        _ = s;
        _ = dir;
        if (ctx.value(u8) < 96) return 0;
        return ctx.value(c_int) | 1;
    }

    pub fn D_800D9B6C(ctx: *zd.Ctx, w: anytype, dir: anytype) c_int {
        _ = w;
        _ = dir;
        if (ctx.value(u8) < 96) return 0;
        return ctx.value(c_int) | 1;
    }

    pub fn func_800E2F5C(ctx: *zd.Ctx, state: anytype, t: anytype, mask: anytype) c_int {
        _ = state;
        _ = t;
        _ = mask;
        if (ctx.value(u8) < 64) return -1 - @as(c_int, ctx.value(u8));
        return @as(c_int, ctx.value(u8));
    }

    pub fn func_800E49D8(ctx: *zd.Ctx, s: anytype, raw: anytype, nearest: anytype, distance: anytype) u32 {
        _ = s;
        _ = raw;
        _ = nearest;
        _ = distance;
        if (ctx.value(u8) < 64) return ctx.value(u32) | 1;
        return 0;
    }

    pub fn func_800D8400(ctx: *zd.Ctx, s: anytype, left: anytype, right: anytype, angle: anytype) u32 {
        _ = left;
        _ = right;
        _ = angle;
        const st = stateOf(s);
        // The C reads timer back after this call.
        if (ctx.value(u1) == 1) st.timer = ctx.value(u16);
        const k = ctx.value(u8);
        if (k < 50) return 0;
        if (k < 100) return (@as(u32, 2) << 24) | @as(u32, ctx.value(u24));
        return ctx.value(u32);
    }

    pub fn func_800E47E4(ctx: *zd.Ctx, s: anytype, first: anytype) c_int {
        _ = s;
        _ = first;
        if (ctx.value(u8) < 100) return 0;
        return ctx.value(c_int) | 1;
    }

    pub fn func_800E4180(ctx: *zd.Ctx, s: anytype, a: anytype, b: anytype, d: anytype) c_int {
        _ = a;
        _ = b;
        _ = d;
        const st = stateOf(s);
        if (ctx.value(u1) == 1) st.timer = ctx.value(u16);
        if (ctx.value(u8) < 100) return 0;
        return ctx.value(c_int) | 1;
    }

    pub fn rcos(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        const r = ctx.value(u8);
        if (r < 24) return 3959;
        if (r < 48) return 3958;
        return ctx.value(i16);
    }
};
