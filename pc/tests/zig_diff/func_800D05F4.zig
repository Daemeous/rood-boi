//! zig_diff spec: func_800D05F4 -> func_800D05F4.func_800D05F4 (5BF94.c).
//! Inputs: a mode word whose bit 7 picks the direction; the shape word's
//! nibbles at their edges (the spreads 0 or not); the limit halfword's top
//! nibble 0 or not (the radius spread); rand returns 0..32767. The libgte
//! calls and vs_battle_lookAt are stubs (see below); vs_gte_rsqrt, rcos and
//! rsin return any int.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D05F4));

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

pub const classes = [_][:0]const u8{ "fixed", "spread" };

pub fn prepare(ctx: *zd.Ctx) Args {
    log = @ptrCast(@alignCast(ctx.allocBytes(4 * 12 * 20, 16)));
    const a = ctx.alloc(v.func_800D0548_arg0);
    a.mode = ctx.int(u16);
    a.shape12 = ctx.pick(u16, &.{ 0, 0x000F, 0x00F0, 0x0010, 0x0F00, 0x8800, 0xFFFF });
    @as(*align(1) u16, @ptrCast(&a.limit)).* = ctx.pick(u16, &.{ 0, 0x0FFF, 0x1000, 0xF000, 0x01E0 });
    return .{ a, ctx.alloc(v.func_800D0548_arg1) };
}

pub fn classify(args: Args) usize {
    const a = args[0].?;
    const limit = @as(*align(1) u16, @ptrCast(&a.limit)).*;
    return if ((limit & 0xF000) != 0 or (a.shape12 & 0xF0) != 0) 1 else 0;
}
