//! zig_diff spec: func_80096444 -> func_80096444.func_80096444 (2842C.c).
//! Inputs: the table pointer D_800F1BAC points at an arena buffer (the C only
//! takes addresses of its elements, so the element size never matters), and
//! the count D_800F227E is a small i16 (-3..11, or near +-32767 for the wrap
//! edges). arg0 is equal to the decremented count, below it (the loop runs
//! 1-5 times), above it (no loop), or 0x7FFFFFFF. The memcpy and bzero
//! callees are void recording stubs: they change only arena bytes, which the
//! C never reads back, and the harness compares the call traces and state.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80096444));

pub const classes = [_][:0]const u8{ "equal", "below", "above" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(4096, 16);
    v.g.D_800F1BAC().* = @ptrCast(buf);

    const wide = ctx.chance(16);
    const d0: i16 = if (wide) ctx.edge(i16, &.{ 32767, -32767 }) else ctx.range(i16, -3, 11);
    v.g.D_800F227E().* = d0;
    const d: c_int = @as(c_int, d0) - 1;

    const arg0: c_int = switch (ctx.range(u8, 0, 9)) {
        0, 1 => d,
        2, 3, 4 => d - ctx.range(c_int, 1, 4),
        5, 6 => d + 1,
        7 => 0x7FFF_FFFF,
        else => d - ctx.range(c_int, 0, 4),
    };
    return .{arg0};
}

pub fn classify(args: Args) usize {
    const d: c_int = @as(c_int, v.g.D_800F227E().*) - 1;
    const arg0 = args[0];
    if (arg0 == d) return 0;
    if (arg0 < d) return 1;
    return 2;
}
