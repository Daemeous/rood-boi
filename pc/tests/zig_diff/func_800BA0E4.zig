//! zig_diff spec: func_800BA0E4 -> func_800BA0E4.func_800BA0E4 (4A0A8.c).
//! Inputs: a 16-byte buffer in the arena whose byte 1 is an edge value (both
//! parities, the sign bit, 0 and 0xFF) and a short argument the C only
//! passes on, never reads. The C's one call, func_80091314, is a recording
//! stub that returns nothing, so the trace compares its argument (arg0[1] ^ 1).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BA0E4));

pub const classes = [_][:0]const u8{ "bit0-clear", "bit0-set" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(16, 1);
    buf[1] = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFE, 0xFF });
    const p: [*c]u8 = buf;
    const arg1 = ctx.pick(c_short, &.{ 0, 1, -1, 32767, -32768 });
    return .{ p, arg1 };
}

pub fn classify(args: Args) usize {
    return if ((args[0][1] & 1) != 0) 1 else 0;
}
