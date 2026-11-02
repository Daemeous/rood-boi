//! zig_diff spec: func_800BA848 -> func_800BA848.func_800BA848 (4A0A8.c).
//! Inputs: a byte buffer in the arena. Byte 1 indexes D_800E9B34 and is kept
//! to 0-5 so the read stays inside the table. Bytes 2-4 are random, with the
//! sign bit set in about half the runs (for a signed read). Byte 4 also takes
//! the 0x7F/0x80 edges. arg1 is random and unused by the C. The callee
//! func_80045CEC is a void recording stub; the C reads nothing back from it,
//! so the default stub is faithful.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BA848));

pub const classes = [_][:0]const u8{ "index-low", "index-high" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(8, 1);
    buf[0] = ctx.int(u8);
    buf[1] = ctx.edge(u8, &.{ 0, 1, 2, 3, 4, 5 });
    buf[2] = ctx.int(u8);
    buf[3] = ctx.int(u8);
    buf[4] = ctx.pick(u8, &.{ 0, 0x7F, 0x80, 0xFF });
    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    return if (args[0][1] < 3) 0 else 1;
}
