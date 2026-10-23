//! zig_diff spec: func_800E7960 -> func_800E7960.func_800E7960 (7EE98.c).
//! Inputs: a tracking-motion state with a valid context and stats block (the
//! stats ratio is current*65536/maximum, maximum never 0 or -1; some pairs
//! give ratios exactly 16383, 16384, 49151 and 49152), a player actor in
//! D_800F4538[0], a trackingMotion behind D_800F5920 (initialised or not,
//! flag3 set or not, phase 0/1/2/255, height around 160, stateC zero or not),
//! and the globals D_800F5918 (the swing) and D_800F591C (the correction).
//! The radius is rsqrt of radiusSquared, so radiusSquared is drawn from the
//! squares of values near 1024 as well as at random. A mode chosen in prepare
//! drives the trig stubs: mode 1 makes every trig value zero (aligned, so the
//! swing lands exactly on 0x820000, 0x40000 and 0x40000-1), mode 3 makes
//! rcos 1, rsin the argument, ratan2 its x argument and the tracking tests
//! true (non-aligned, so the swing lands exactly on zero, which makes the
//! correction zero too). Mode 0 is random trig. Some mode-0 runs put the
//! correction at 1100 and the height at 767, so targetY lands exactly on
//! 0xFF000. The `height < 160` compare inside the `difference >= 16` branch
//! can never see height 159 (that would take the `difference < 16` branch),
//! so its equality is waived. vs_gte_rsqrt is the integer square root (the
//! reviewer's change) and two thirds of radiusSquared are the edge squares.
//! targetY is a multiple of 0x1000, so GCC's form of `>= 0xFF000` (`> 0xFEFFF`)
//! never sees its sides equal: waived (0xFF000 itself is drawn).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_7EE98");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E7960));

pub const classes = [_][:0]const u8{ "uninitialised", "stateC-set", "clamped-D", "low-ratio", "other" };

/// 0: random trig; 1: trig all zero; 3: trig identity and tracking forced.
var mode: u8 = 0;

const radius_squares = [_]i32{ 261121, 262144, 1044484, 1046529, 1048576, 1050625, 1052676, 4190209, 4194304, 1048575, 984064, 1115136, 1000000, 1060900 };
const d_edges = [_]i32{ 0, 1, -1, 0x280000, 0x280001, -0x280001, 0x820000, 0x820001, -0x820000, -0x820001, 0x40000, 0x3FFFF, 65536, 65535, -65536, 131072, 100000, -100000 };
const k_edges = [_]i32{ -100, -101, -95, -99, 29, 30, 31, 499, 500, 501, 0, 1 };
const mode1_d = [_]i32{ 7514795, -7514795, 196608, -196608, 196607, -196607 };
/// (current, maximum) pairs whose ratio is 16384, 49152, 49151 and 16383.
const pairs = [_][2]i16{ .{ 3, 4 }, .{ 24575, 32767 }, .{ 1, 4 }, .{ 4096, 16385 } };

fn motionOf() *v.trackingMotion {
    const p: *v.D_800F5920_t = v.g.D_800F5920().*.?;
    return @ptrCast(@alignCast(p));
}

fn pickI32(ctx: *zd.Ctx, edges: []const i32, lo: i32, hi: i32) i32 {
    if (ctx.chance(2)) return edges[ctx.range(usize, 0, edges.len - 1)];
    return ctx.range(i32, lo, hi);
}

pub const stubs = struct {
    // The real vs_gte_rsqrt is the integer square root (a recording stub's
    // random roots never put the radius on its 1023/1024 edges or within 32 of 1024).
    pub fn vs_gte_rsqrt(ctx: *zd.Ctx, a: c_int) c_int {
        _ = ctx;
        if (a <= 0) return 0;
        return @intCast(std.math.sqrt(@as(u32, @intCast(a))));
    }

    pub fn rcos(ctx: *zd.Ctx, a: c_int) c_int {
        _ = a;
        if (mode == 1) return 0;
        if (mode == 3) return 1;
        if (ctx.value(u8) < 64) return 0;
        return @as(c_int, ctx.value(i13));
    }

    pub fn rsin(ctx: *zd.Ctx, a: c_int) c_int {
        if (mode == 1) return 0;
        if (mode == 3) return a;
        if (ctx.value(u8) < 64) return 0;
        return @as(c_int, ctx.value(i13));
    }

    pub fn ratan2(ctx: *zd.Ctx, y: anytype, x: anytype) c_int {
        _ = y;
        if (mode == 3) return @as(c_int, x);
        return switch (ctx.value(u3)) {
            0 => 0,
            1 => 0x800,
            2 => 0x400,
            else => @as(c_int, ctx.value(u12)),
        };
    }

    pub fn func_800E4660(ctx: *zd.Ctx, a: anytype, b: anytype, z: anytype) c_uint {
        _ = a;
        _ = b;
        _ = z;
        if (ctx.value(u4) == 0) return 1048576;
        return @as(c_uint, ctx.value(u20) | 1);
    }

    // The callee may set the motion's stateC, which the C tests afterwards.
    pub fn func_800E78F4(ctx: *zd.Ctx, s: anytype) c_int {
        _ = s;
        if (mode == 3) return 1;
        if (mode == 0 and ctx.value(u8) < 64) motionOf().stateC = ctx.value(i16);
        return if (ctx.value(u8) < 160) 1 else 0;
    }

    pub fn func_800D826C(ctx: *zd.Ctx, s: anytype) c_int {
        _ = s;
        if (mode == 3) return 1;
        return if (ctx.value(bool)) 1 else 0;
    }

    // Changes only the state, which the C does not read after the call.
    pub fn func_800DEEFC(ctx: *zd.Ctx, s: anytype, n: c_int) void {
        _ = ctx;
        _ = s;
        _ = n;
    }

    pub fn rand(ctx: *zd.Ctx) c_int {
        if (ctx.value(u8) < 8) return 0;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.alloc(v.trackingMotionState);
    const context = ctx.alloc(v.radialMotionContext);
    const stats = ctx.alloc(v.trackingMotionStats);
    const actor = ctx.alloc(v.trackingMotionActor);
    const motion = ctx.alloc(v.trackingMotion);

    psx.put(&state.context, context);
    psx.put(&state.stats, stats);

    const roll = ctx.range(u8, 0, 99);
    mode = if (roll < 30) 1 else if (roll < 50) 3 else 0;

    state.x = ctx.pick(i16, &.{ 0, 0x7C0, 0x7C1, -1, 32767, -32768 });
    state.y = ctx.pick(i16, &.{ 160, 144, 176, 0, -1, 32767, -32768 });
    state.z = ctx.pick(i16, &.{ 0x7C0, -1, 0, 32767, -32768 });

    // maximum is never 0 (the C divides by it) nor -1 (INT_MIN / -1 traps).
    if (ctx.chance(2)) {
        const p = pairs[ctx.range(usize, 0, pairs.len - 1)];
        stats.current = p[0];
        stats.maximum = p[1];
    } else {
        stats.current = ctx.pick(i16, &.{ 0, 1, 2, 3, 4, -1, 32767, -32768 });
        // (edges only: ctx.pick returns a random value half the time, 0 and -1 included)
        const maxima = [_]i16{ 1, 2, 3, -2, 32767, -32768 };
        stats.maximum = if (ctx.chance(2)) 4 else maxima[ctx.range(usize, 0, maxima.len - 1)];
    }

    actor.x = ctx.pick(i16, &.{ 0x7C0, 0, -1, 32767, -32768 });
    actor.y = ctx.int(i16);
    actor.z = ctx.pick(i16, &.{ 0x7C0, 0, -1, 32767, -32768 });
    const d4538: *v.D_800F4538_t = @ptrCast(@alignCast(actor));
    v.g.D_800F4538()[0] = d4538;

    motion.facing = ctx.int(u16);
    motion.state = ctx.int(u8);
    motion.flag3 = if (ctx.chance(3)) 0 else 1;
    motion.radiusSquared = if (!ctx.chance(3))
        radius_squares[ctx.range(usize, 0, radius_squares.len - 1)]
    else
        ctx.range(i32, 1, 0x40000000);
    motion.stateC = if (ctx.chance(4)) ctx.int(i16) else 0;
    motion.stateE = ctx.int(i16);
    motion.phase = if (ctx.chance(2)) ctx.range(u8, 0, 1) else ctx.pick(u8, &.{ 0, 1, 2, 255 });
    motion.height = ctx.pick(i16, &.{ 160, 144, 176, 145, 175, 0, -1, 32767, -32768, 100, 200 });
    motion.initialized = if (ctx.chance(3)) 0 else 1;
    const mp: *v.D_800F5920_t = @ptrCast(@alignCast(motion));
    v.g.D_800F5920().* = mp;

    var d: i32 = pickI32(ctx, &d_edges, -0x900000, 0x900000);
    var k: i32 = pickI32(ctx, &k_edges, -300, 700);

    switch (mode) {
        1 => {
            // Aligned with zero trig: the swing lands on the clamp, the 0x40000 bound.
            d = mode1_d[ctx.range(usize, 0, mode1_d.len - 1)];
            motion.stateC = 0;
            motion.phase = if (ctx.chance(2)) 2 else 255;
        },
        3 => {
            // Non-aligned, tracking: the swing lands exactly on zero.
            d = if (ctx.chance(2)) 87381 else -87381;
            actor.z = if (d > 0) 100 else 200;
            state.z = if (d > 0) 200 else 100;
        },
        else => {
            // Zero swing, correction 1100 (clamped to 500), height 767 (to 755): targetY == 0xFF000.
            if (ctx.chance(8)) {
                d = 0;
                k = 1100;
                motion.height = 767;
            }
        },
    }

    v.g.D_800F5918().* = d;
    v.g.D_800F591C().* = k;

    return .{state};
}

pub fn classify(args: Args) usize {
    const state = args[0].?;
    const m = motionOf();
    if (m.initialized == 0) return 0;
    if (m.stateC != 0) return 1;
    if (@abs(v.g.D_800F5918().*) > 0x820000) return 2;
    const stats = psx.get(state.stats).?;
    const ratio = @divTrunc(@as(i32, stats.current) * 65536, @as(i32, stats.maximum));
    if (ratio < 0xC000) return 3;
    return 4;
}
