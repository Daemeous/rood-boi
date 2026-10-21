//! zig_diff spec: func_800A1B9C -> func_800A1B9C.func_800A1B9C (38C1C.c).
//! The unit is a two-call chain: the first callee's result gates the second
//! call, and the second's result is returned when it is not negative. The
//! inputs pick each callee's return value (negative, zero, or positive, with
//! the sign bit and INT_MIN/INT_MAX as edges) through spec-level globals the
//! recording stubs return, so every branch is taken: first negative, second
//! negative, both non-negative. The arguments are random ints and an optional
//! null arg2; the C only forwards them, so the call traces must match.
const std = @import("std");
const zd = @import("zdiff");
const common = @import("common");
const psx = common.psx;
const v = @import("views_38C1C");
const c = @import("c_38C1C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A1B9C));

/// Return values the stubs hand back, chosen by prepare.
var first_ret: c_int = 0;
var second_ret: c_int = 0;

pub const stubs = struct {
    pub fn func_800A152C(ctx: *zd.Ctx, a: anytype, b: anytype, d: anytype) c_int {
        _ = ctx;
        _ = a;
        _ = b;
        _ = d;
        return first_ret;
    }

    pub fn func_800A1C10(ctx: *zd.Ctx, a: anytype, b: anytype, d: anytype, e: anytype) c_int {
        _ = ctx;
        _ = a;
        _ = b;
        _ = d;
        _ = e;
        return second_ret;
    }
};

pub const classes = [_][:0]const u8{ "first-negative", "second-negative", "both-ok" };

fn negative(ctx: *zd.Ctx) c_int {
    if (ctx.chance(3)) return -1;
    if (ctx.chance(5)) return std.math.minInt(c_int);
    return ctx.range(c_int, std.math.minInt(c_int), -1);
}

fn nonNegative(ctx: *zd.Ctx) c_int {
    if (ctx.chance(2)) return 0;
    if (ctx.chance(6)) return std.math.maxInt(c_int);
    return ctx.range(c_int, 0, std.math.maxInt(c_int));
}

pub fn prepare(ctx: *zd.Ctx) Args {
    // The first callee's result: negative a third of the time.
    first_ret = if (ctx.chance(3)) negative(ctx) else nonNegative(ctx);
    // The second callee's result: only consulted when the first is not negative.
    second_ret = if (ctx.chance(3)) negative(ctx) else nonNegative(ctx);

    const arg0 = ctx.pick(c_int, &.{ 0, 1, -1, std.math.maxInt(c_int), std.math.minInt(c_int) });
    const arg1 = ctx.pick(c_int, &.{ 0, 1, -1, std.math.maxInt(c_int), std.math.minInt(c_int) });
    const arg3 = ctx.pick(c_int, &.{ 0, 1, -1, std.math.maxInt(c_int), std.math.minInt(c_int) });

    // arg2 is forwarded, never dereferenced by the C: null sometimes.
    var arg2: [*c]c_ushort = null;
    if (!ctx.chance(4)) arg2 = ctx.alloc(c_ushort);

    return .{ arg0, arg1, arg2, arg3 };
}

pub fn classify(args: Args) usize {
    _ = args;
    if (first_ret < 0) return 0;
    if (second_ret < 0) return 1;
    return 2;
}
