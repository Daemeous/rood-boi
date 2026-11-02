//! zig_diff spec: func_800B68C4 -> func_800B68C4.func_800B68C4 (4A0A8.c).
//! Inputs: a buffer in the arena passed as arg0 (any content), an arbitrary
//! short arg1 (unused by the C), and D_800F4C2C set to 2 (the branch that
//! calls func_800B6778 and returns 4) or to another value (the branch that
//! returns 0), including 0x82, which is -126 as a char and must not match 2.
//! The callee stub writes the buffer's first byte and, in a quarter of the
//! calls, D_800F4C2C, which the C never reads back after the call.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B68C4));

pub const classes = [_][:0]const u8{ "two", "other" };

pub const stubs = struct {
    pub fn func_800B6778(ctx: *zd.Ctx, p: anytype) void {
        const b: *u8 = @ptrCast(p);
        b.* = ctx.value(u8);
        if (ctx.value(u2) == 0) v.g.D_800F4C2C().* = ctx.value(u8);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(16, 16);
    v.g.D_800F4C2C().* = ctx.edge(u8, &.{ 2, 2, 2, 0x82, 0, 0xFF, 1, 0x42 });
    return .{ buf, ctx.int(i16) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (v.g.D_800F4C2C().* == 2) 0 else 1;
}
