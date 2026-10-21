//! zig_diff spec: func_800A1B28 -> func_800A1B28.func_800A1B28 (38C1C.c).
//! Inputs: arbitrary arg0, arg1 and arg3 (edge values and random ints) and a
//! buffer pointer that is sometimes null. The two callees are recording stubs
//! whose results are chosen by prepare: the first call returns negative, zero
//! or positive, and so does the second, so both `if (var_v0 < 0)` tests take
//! both branches. Negative results carry the sign bit, nonnegative ones clear
//! it, and zero is returned often, so each comparison sees equal sides.
const std = @import("std");
const zd = @import("zdiff");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A1B28));

pub const classes = [_][:0]const u8{
    "first-negative",
    "first-zero-second-negative",
    "first-zero-second-nonnegative",
    "first-positive-second-negative",
    "first-positive-second-nonnegative",
};

/// Outcome per callee for this iteration: 0 negative, 1 zero, 2 positive.
/// Set by prepare before the C run; both runs' stubs read the same values.
var first_mode: u8 = 0;
var second_mode: u8 = 0;

fn result(ctx: *zd.Ctx, mode: u8) c_int {
    const x = ctx.value(u32);
    return switch (mode) {
        0 => @as(c_int, @bitCast(x | 0x8000_0000)),
        1 => 0,
        else => @as(c_int, @bitCast(x & 0x7FFF_FFFF)),
    };
}

pub const stubs = struct {
    pub fn func_800A152C(ctx: *zd.Ctx, a: c_int, b: c_int, c: c_int) c_int {
        _ = a;
        _ = b;
        _ = c;
        return result(ctx, first_mode);
    }

    pub fn func_800A1C10(ctx: *zd.Ctx, a: c_int, b: c_int, p: [*c]c_ushort, d: c_int) c_int {
        _ = a;
        _ = b;
        _ = p;
        _ = d;
        return result(ctx, second_mode);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    first_mode = ctx.range(u8, 0, 2);
    second_mode = ctx.range(u8, 0, 2);
    const edges = [_]c_int{ 0, 1, -1, 2, std.math.maxInt(c_int), std.math.minInt(c_int) };
    const buf: [*c]c_ushort = @ptrCast(ctx.alloc(u16));
    const arg2: [*c]c_ushort = if (ctx.chance(4)) null else buf;
    return .{
        ctx.pick(c_int, &edges),
        ctx.pick(c_int, &edges),
        arg2,
        ctx.pick(c_int, &edges),
    };
}

pub fn classify(args: Args) usize {
    _ = args;
    if (first_mode == 0) return 0;
    if (first_mode == 1) return if (second_mode == 0) 1 else 2;
    return if (second_mode == 0) 3 else 4;
}
