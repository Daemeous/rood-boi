//! zig_diff spec: func_800D0984 -> func_800D0984.func_800D0984 (5BF94.c).
//! Inputs: the scratchpad camera's words random, an arena effect, push 0
//! or not.
//! The libgte calls and vs_battle_lookAt are stubs (see below); vs_gte_rsqrt
//! returns any int (the 0x600 offset narrows to 16 bits).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D0984));

var log: ?[*]i32 = null;

/// The C's stack vectors and matrices reach the libgte stubs as tags: each
/// stub logs the words the C set (never a pad) at a slot per call, and
/// writes the outputs the C then reads.
fn put(ctx: *zd.Ctx, k: usize, words: []const i32) void {
    const base = 12 * @as(usize, ctx.calls) + k;
    for (words, 0..) |x, i| log.?[base + i] = x;
}

fn vec(p: anytype) [3]i32 {
    const s: *v.VECTOR = @ptrCast(@alignCast(p));
    return .{ s.vx, s.vy, s.vz };
}

fn svec(p: anytype) [3]i32 {
    const s: *v.SVECTOR = @ptrCast(@alignCast(p));
    return .{ s.vx, s.vy, s.vz };
}

pub const stubs = struct {
    pub fn vs_battle_lookAt(ctx: *zd.Ctx, from: anytype, to: anytype, mat: anytype) void {
        put(ctx, 0, &vec(from));
        put(ctx, 3, &vec(to));
        const m: *v.MATRIX = @ptrCast(@alignCast(mat));
        for (0..3) |i| for (0..3) |j| {
            m.m[i][j] = ctx.value(i16);
        };
    }
    pub fn SetRotMatrix(ctx: *zd.Ctx, mat: anytype) void {
        const m: *v.MATRIX = @ptrCast(@alignCast(mat));
        var words: [9]i32 = undefined;
        for (0..3) |i| for (0..3) |j| {
            words[3 * i + j] = m.m[i][j];
        };
        put(ctx, 0, &words);
    }
    pub fn SetTransMatrix(ctx: *zd.Ctx, mat: anytype) void {
        const m: *v.MATRIX = @ptrCast(@alignCast(mat));
        put(ctx, 0, &m.t);
    }
    pub fn RotTrans(ctx: *zd.Ctx, in: anytype, out: anytype, flag: anytype) void {
        put(ctx, 0, &svec(in));
        const o: *v.VECTOR = @ptrCast(@alignCast(out));
        o.vx = ctx.value(i32);
        o.vy = ctx.value(i32);
        o.vz = ctx.value(i32);
        flag.* = ctx.value(c_int);
    }
    pub fn rand(ctx: *zd.Ctx) c_int {
        return ctx.value(u15);
    }
};

pub const classes = [_][:0]const u8{ "placed", "pushed" };

pub fn prepare(ctx: *zd.Ctx) Args {
    log = @ptrCast(@alignCast(ctx.allocBytes(4 * 12 * 8, 16)));
    return .{ ctx.int(c_int), ctx.alloc(v.func_800D0C60_t), ctx.pick(c_int, &.{ 0, 0, 1, -1 }) };
}

pub fn classify(args: Args) usize {
    return if (args[2] != 0) 1 else 0;
}
