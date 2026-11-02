//! zig_diff spec: func_800B86E8 -> func_800B86E8.func_800B86E8 (4A0A8.c).
//! The C ignores both arguments and returns `func_8007CCAC() > 0`, so the inputs
//! vary arg0 (null or an arena byte) and arg1 (any short, both signs). The stub
//! for func_8007CCAC returns 0 about 38% of the time, so the `>` sees equal sides
//! often. It also returns exactly 1 (for the `> 1` variant), INT_MIN (negative
//! results, for the unsigned and raw-return mutations) and any other 32-bit value.
const std = @import("std");
const zd = @import("zdiff");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B86E8));

pub const classes = [_][:0]const u8{ "null-arg0", "neg-arg1", "pos-arg1" };

pub fn prepare(ctx: *zd.Ctx) Args {
    var p: [*c]u8 = null;
    if (!ctx.chance(4)) p = ctx.allocBytes(8, 1);
    return .{ p, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    if (args[0] == null) return 0;
    return if (args[1] < 0) 1 else 2;
}

pub const stubs = struct {
    pub fn func_8007CCAC(ctx: *zd.Ctx) c_int {
        if (ctx.value(u8) < 96) return 0;
        if (ctx.value(u8) < 40) return 1;
        if (ctx.value(u8) < 16) return std.math.minInt(c_int);
        return ctx.value(c_int);
    }
};
