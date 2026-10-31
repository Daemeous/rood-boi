//! zig_diff spec: func_80074580 -> func_80074580.func_80074580 (146C.c).
//! Inputs: vs_main_projectionDistance across the 272 bound (negative, just
//! below, equal, just above, the i32 extremes), so the `<` test sees its sides
//! equal and the signed compare is exercised. _stepCameraTransition returns 0,
//! positive, -1 and arbitrary c_int values, so `!= 0` sees values with and
//! without the sign bit. The callees are recording stubs; their results are
//! whatever ctx.value draws, so the bits passed through `& ~2` vary per run.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80074580));

pub const classes = [_][:0]const u8{ "near", "at-272", "far" };

pub const stubs = struct {
    pub fn _stepCameraTransition(ctx: *zd.Ctx) c_int {
        return switch (ctx.value(u8)) {
            0...63 => 0,
            64...127 => 1,
            128...191 => -1,
            else => ctx.value(c_int),
        };
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const dist = v.g.vs_main_projectionDistance();
    dist.* = ctx.pick(i32, &.{ 271, 272, 273, -1, 0, std.math.maxInt(i32), std.math.minInt(i32) });
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const d = v.g.vs_main_projectionDistance().*;
    if (d < 272) return 0;
    if (d == 272) return 1;
    return 2;
}
