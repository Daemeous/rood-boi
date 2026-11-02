//! zig_diff spec: func_800B983C -> func_800B983C.func_800B983C (4A0A8.c).
//! The C reads one byte, arg0[1], and passes it to func_800919D8 (widened to
//! c_int), then returns 0; arg1 is unused. Inputs: an arena buffer or a buffer
//! in the scratchpad (never null, since the C dereferences arg0 without a
//! test), with byte 1 at zero, at the sign bit (0x80, 0xFF), or at other
//! values, and any c_short for arg1. The callee stub returns a value the C
//! ignores; it changes no state the C reads afterwards.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B983C));

pub const stubs = struct {
    pub fn func_800919D8(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        return ctx.value(c_int);
    }
};

pub const classes = [_][:0]const u8{ "byte-zero", "byte-sign-bit", "byte-other" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const p: [*]u8 = if (ctx.chance(4))
        @ptrFromInt(zd.scratchpad + ctx.range(usize, 0, 0xFFE))
    else
        ctx.allocBytes(4, 1);
    p[0] = ctx.int(u8);
    p[1] = ctx.pick(u8, &.{ 0, 0x7F, 0x80, 0xFF, 1 });
    return .{ p, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const b = args[0][1];
    if (b == 0) return 0;
    return if ((b & 0x80) != 0) 1 else 2;
}
