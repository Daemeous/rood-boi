//! zig_diff spec: func_800AECA0 -> func_800AECA0.func_800AECA0 (44F14.c).
//! Inputs: a MATRIX `source` that is either in the arena, is the scratchpad
//! matrix itself (so the final `source->t` reads what the C just wrote), or is
//! the global D_800F4910 (which the C zeroes and then stores a pointer into).
//! The camera angle is any i32 except INT_MIN, and the zoom (the divisor) is
//! a nonzero i32 with both signs, including values whose unsigned reading
//! differs. The scratchpad and D_8005E218 keep their random bytes. The
//! rcos/rsin results are any c_int, so the i16 narrowing wraps. ScaleMatrix
//! and func_80041C68 write their matrix arguments with random values, as the
//! real callees do.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_44F14");
const c = @import("c_44F14");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AECA0));

pub const classes = [_][:0]const u8{ "arena-source", "scratchpad-source", "global-source" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const cam_angle: *i32 = @ptrFromInt(0x1F80005C);
    const cam_zoom: *i32 = @ptrFromInt(0x1F800064);
    var angle = ctx.int(i32);
    if (angle == std.math.minInt(i32)) angle = 0; // -INT_MIN is undefined in the C
    cam_angle.* = angle;
    // Never zero: the C divides by it. Both signs, and values whose unsigned reading differs.
    cam_zoom.* = ctx.edge(i32, &.{ 1, -1, 2, 4096, -4096, 0x1000000, 0x7FFFFFFF, -0x7FFFFFFF });

    const which = ctx.range(u8, 0, 2);
    const source: *v.MATRIX = switch (which) {
        0 => ctx.alloc(v.MATRIX),
        1 => @ptrFromInt(0x1F8003C0), // the scratchpad matrix: the C overwrites it before the final reads
        else => v.g.D_800F4910(),
    };
    return .{source};
}

pub fn classify(args: Args) usize {
    const p = @intFromPtr(args[0].?);
    if (p == 0x1F8003C0) return 1;
    if (p == @intFromPtr(v.g.D_800F4910())) return 2;
    return 0;
}

fn scramble(ctx: *zd.Ctx, p: anytype) void {
    const m: *v.MATRIX = @ptrCast(@alignCast(p));
    for (0..3) |i| for (0..3) |j| {
        m.m[i][j] = ctx.value(i16);
    };
    for (0..3) |i| m.t[i] = ctx.value(i32);
}

pub const stubs = struct {
    pub fn rcos(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        return ctx.value(c_int);
    }

    pub fn rsin(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        return ctx.value(c_int);
    }

    pub fn ScaleMatrix(ctx: *zd.Ctx, m: anytype, vec: anytype) [*c]c.MATRIX {
        _ = vec;
        scramble(ctx, m);
        return null;
    }

    pub fn func_80041C68(ctx: *zd.Ctx, a: anytype, b: anytype) void {
        scramble(ctx, a);
        scramble(ctx, b);
    }
};
