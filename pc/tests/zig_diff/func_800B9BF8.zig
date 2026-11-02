//! zig_diff spec: func_800B9BF8 -> func_800B9BF8.func_800B9BF8 (4A0A8.c).
//! Inputs: none (the function takes no arguments and reads no globals). The
//! only thing it depends on is the return value of func_8007C398. prepare
//! picks that value's class: 0, 1, -1 and INT_MIN (the sign bit alone), or a
//! random c_int. The stub returns the picked value, so the `!= 0` test sees
//! zero and nonzero values, with and without the sign bit, and the C's call
//! trace is compared on every run.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B9BF8));

pub const classes = [_][:0]const u8{ "zero", "one", "minus-one", "int-min", "random" };

/// The class prepare picked, and the value the stub returns for it.
var kind: usize = 0;
var result: c_int = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    kind = ctx.range(usize, 0, 4);
    result = switch (kind) {
        0 => 0,
        1 => 1,
        2 => -1,
        3 => std.math.minInt(c_int),
        else => ctx.value(c_int),
    };
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return kind;
}

pub const stubs = struct {
    pub fn func_8007C398(ctx: *zd.Ctx) c_int {
        _ = ctx;
        return result;
    }
};
