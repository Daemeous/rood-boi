//! zig_diff spec: func_800A13EC -> func_800A13EC.func_800A13EC (30DB0.c).
//! Inputs: arg0, arg1 and arg3 from edge values (0, 1, -1, INT_MAX, INT_MIN)
//! and random ints; arg2 is an arena int (psx_addr is below 4 GB) or null.
//! The callee's result is steered by `want`, which prepare sets and classify
//! reads: 0 (equal to the C's `< 0` bound), negative (the early return) and
//! positive (the return 0). The callee's values span the whole c_int range.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A13EC));

pub const classes = [_][:0]const u8{ "zero", "negative", "positive" };

var want: u8 = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    want = ctx.range(u8, 0, 2);
    const arg0 = ctx.pick(c_int, &.{ 0, 1, -1, 0x7fffffff, -2147483648 });
    const arg1 = ctx.pick(c_int, &.{ 0, 1, -1, 0x7fffffff, -2147483648 });
    const arg3 = ctx.pick(c_int, &.{ 0, 1, -1, 0x7fffffff, -2147483648 });
    const arg2: [*c]c_int = if (ctx.chance(6)) null else @ptrCast(ctx.alloc(c_int));
    return .{ arg0, arg1, arg2, arg3 };
}

pub fn classify(args: Args) usize {
    _ = args;
    return want;
}

pub const stubs = struct {
    pub fn func_800A141C(ctx: *zd.Ctx, _: anytype, _: anytype, _: anytype, _: anytype) c_int {
        return switch (want) {
            0 => 0,
            1 => -1 - @as(c_int, @intCast(ctx.value(u31))),
            else => @as(c_int, @intCast(ctx.value(u31))),
        };
    }
};
