//! zig_diff spec: func_80091320 -> func_80091320.func_80091320 (2842C.c).
//! Inputs: the argument, drawn from edge values (0, +-1, the i16 and u16
//! boundaries, INT_MIN and INT_MAX) and otherwise any c_int. The C stores
//! it in the i16 global D_800F1DC4 (so the high bits are dropped) and then
//! calls two externals, both of which may rewrite that global: the stubs
//! write it with ctx.value, so a port that reads it back after the calls, or
//! stores it at the wrong point, differs in the call traces.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80091320));

pub const classes = [_][:0]const u8{ "negative", "non-negative" };

pub const stubs = struct {
    pub fn func_8009121C(ctx: *zd.Ctx) void {
        if (ctx.value(u1) == 1) v.g.D_800F1DC4().* = ctx.value(i16);
    }

    pub fn func_80090B28(ctx: *zd.Ctx) void {
        if (ctx.value(u1) == 1) v.g.D_800F1DC4().* = ctx.value(i16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg = ctx.pick(c_int, &.{
        0,
        -1,
        1,
        0x7FFF,
        -0x8000,
        0x8000,
        0xFFFF,
        0x10000,
        std.math.maxInt(c_int),
        std.math.minInt(c_int),
    });
    return .{arg};
}

pub fn classify(args: Args) usize {
    return if (args[0] < 0) 0 else 1;
}
