//! zig_diff spec: func_800B6F8C -> func_800B6F8C.func_800B6F8C (4A0A8.c).
//! Inputs: a 4-byte buffer in the arena whose byte 1 is an edge value (0, 1,
//! 0x7f, 0x80, 0xff) or any byte, so the signed and unsigned reads of arg0[1]
//! differ; arg1 is random and unused by the C. The callee
//! vs_battle_dismissTextBox is a recording stub that returns 0, 1, -1 or any
//! 32-bit value, so the `!= 0` test sees zero and nonzero results with and
//! without the sign bit.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B6F8C));

pub const classes = [_][:0]const u8{ "byte-zero", "byte-high", "byte-low" };

pub const stubs = struct {
    pub fn vs_battle_dismissTextBox(ctx: *zd.Ctx, arg: c_int) c_int {
        _ = arg;
        const k = ctx.value(u8);
        if (k < 64) return 0;
        if (k < 128) return 1;
        if (k < 160) return -1;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(4, 1);
    buf[0] = ctx.int(u8);
    buf[1] = ctx.pick(u8, &.{ 0, 1, 0x7f, 0x80, 0xff });
    buf[2] = ctx.int(u8);
    buf[3] = ctx.int(u8);
    const p: [*c]u8 = buf;
    return .{ p, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const b = args[0][1];
    if (b == 0) return 0;
    if (b >= 0x80) return 1;
    return 2;
}
