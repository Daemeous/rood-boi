//! zig_diff spec: func_800BA35C -> func_800BA35C.func_800BA35C (4A0A8.c).
//! Inputs: a 16-byte arena buffer whose byte 3 (the C's `arg0[3]`) is an
//! edge value (0, 1, 127, 128, 255) or random, and any `arg1` (the C never
//! reads it). The C has no branches; it passes `vs_battle_getShort(arg0 + 1)`
//! through `func_800BFE50` and `arg0[3]` to `func_8007C8A4`, then returns 0.
//! The callees are recording stubs: `vs_battle_getShort` returns any
//! c_short (so the u8 narrowing of the `func_800BFE50` argument is
//! exercised), and `func_800BFE50` returns any c_int. The callees do not
//! write to `arg0`: the C reads `arg0[3]` as an argument, and the order of
//! argument evaluation in C is unspecified, so a stub write there would make
//! the result depend on evaluation order.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BA35C));

pub const classes = [_][:0]const u8{ "byte3-high", "byte3-low" };

pub const stubs = struct {
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = p;
        return ctx.value(c_short);
    }

    pub fn func_800BFE50(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf: [*c]u8 = @ptrCast(ctx.allocBytes(16, 16));
    buf[3] = ctx.pick(u8, &.{ 0, 1, 127, 128, 255 });
    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    return if (args[0][3] >= 128) 0 else 1;
}
