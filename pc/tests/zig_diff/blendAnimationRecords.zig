//! zig_diff spec: func_800B147C -> pc.blend_animation_records.blendAnimationRecords (D2: pc/src/actor_blend.c, through its static blend_vector).
//! Inputs: two pose records of 0x700 random-ish bytes in the arena (shorts of
//! three kinds: any, small, or the destination plus a delta from an edge list
//! around the half-turn wrap 4096 and 8192, whole vectors equal a third of the
//! time), a bone count (1..42, or 0, negative or 43..45, where pc_unimplemented's
//! stub returns and the loop runs on), and a step and duration that choose the
//! copy (equal), halfway (step == duration >> 1, logical) or weighted path; a zero
//! duration (the R3000's divide without a zero check) and the INT_MIN / -1
//! overflow (avoided: it traps in both) included. The GTE is a set of recording
//! stubs; MFC2 returns zero a third of the time, so the stores' conditions vary.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").blendAnimationRecords));

pub const classes = [_][:0]const u8{ "copy", "half", "blend", "bad-extent", "zero-duration" };

const record_size = 0x700;
const delta_edges = [_]c_int{ 0, 1, -1, 4095, 4096, 4097, -4095, -4096, -4097, 8191, 8192, -8192, 0x7fff, -0x8000 };

fn vectors(ctx: *zd.Ctx, dst: [*]u8, src: [*]u8, offset: usize, step: usize, count: usize) void {
    for (0..count) |k| {
        const d: [*]align(1) i16 = @ptrCast(dst + offset + k * step);
        const s: [*]align(1) i16 = @ptrCast(src + offset + k * step);
        const mode = ctx.range(u8, 0, 5);
        for (0..3) |j| {
            d[j] = switch (mode) {
                0 => ctx.int(i16),
                else => ctx.range(i16, -0x3000, 0x3000),
            };
            s[j] = switch (mode) {
                0 => ctx.int(i16),
                1, 2 => ctx.range(i16, -0x3000, 0x3000),
                3 => d[j],
                else => @truncate(@as(c_int, d[j]) +% ctx.edge(c_int, &delta_edges)),
            };
        }
        if (mode == 5 and ctx.chance(2)) s[2] = d[2];
    }
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const dst = ctx.allocBytes(record_size, 16);
    const src = ctx.allocBytes(record_size, 16);
    for (dst[0..record_size]) |*byte| byte.* = ctx.int(u8);
    for (src[0..record_size]) |*byte| byte.* = ctx.int(u8);
    vectors(ctx, dst, src, 0x148, 8, 1);
    vectors(ctx, dst, src, 0, 8, 46);
    vectors(ctx, dst, src, 0x150, 8, 46);
    const count: c_int = switch (ctx.range(u8, 0, 9)) {
        0 => ctx.edge(c_int, &.{ 0, -1, -0x80000000, 43, 44, 45 }),
        1 => 42,
        else => ctx.range(c_int, 1, 42),
    };
    const duration: c_int = ctx.pick(c_int, &.{ 0, 1, 2, 3, 4, 100, 4096, -1, -2, -4, 0x7fffffff, -0x80000000 });
    var step: c_int = switch (ctx.range(u8, 0, 9)) {
        0, 1 => duration,
        2, 3 => @bitCast(@as(c_uint, @bitCast(duration)) >> 1),
        4 => ctx.edge(c_int, &.{ 1, -1, 0x80000, -0x80000, 0x7ffff, 0x100000, -0x100000 }),
        else => ctx.range(c_int, -9, 9),
    };
    // Also with the Q11 weight of a mutation (<< 11): INT_MIN / -1 traps there as well.
    if (duration == -1 and (@as(c_int, @bitCast(@as(c_uint, @bitCast(step)) << 12)) == -0x80000000 or
        @as(c_int, @bitCast(@as(c_uint, @bitCast(step)) << 11)) == -0x80000000)) step = 1;
    return .{ dst, src, count, step, duration };
}

pub fn classify(args: Args) usize {
    const step = args[3];
    const duration = args[4];
    if (step == duration) return 0;
    if (step == @as(c_int, @bitCast(@as(c_uint, @bitCast(duration)) >> 1))) return 1;
    if (args[2] <= 0 or args[2] > 42) return 3;
    return if (duration == 0) 4 else 2;
}

pub const stubs = struct {
    pub fn MFC2(ctx: *zd.Ctx, reg: anytype) c_uint {
        _ = reg;
        if (ctx.value(u8) % 3 == 0) return 0;
        return ctx.value(c_uint);
    }
};

/// The message pc_unimplemented gets is the C's literal on one side and the port's on the other: compared by text.
pub fn textArgument(call: []const u8, index: usize) bool {
    return std.mem.eql(u8, call, "pc_unimplemented") and index == 0;
}
