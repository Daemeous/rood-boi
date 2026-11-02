//! zig_diff spec: func_800B91C4 -> func_800B91C4.func_800B91C4 (4A0A8.c).
//! Inputs: a 3-byte buffer the C reads at offsets 1 and 2, either in the arena
//! or in the scratchpad, filled with edge bytes (0, 1, 0x7F, 0x80, 0xFF) or any
//! byte, plus any c_short for arg1 (which the C ignores). The callee is a void
//! recording stub that changes nothing the C reads back, and the C returns 0.
const std = @import("std");
const zd = @import("zdiff");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B91C4));

pub const classes = [_][:0]const u8{ "arena", "scratchpad" };

pub fn prepare(ctx: *zd.Ctx) Args {
    var buf: [*c]u8 = undefined;
    if (ctx.chance(4)) {
        // A scratchpad buffer, the last three bytes included.
        buf = @ptrFromInt(zd.scratchpad + ctx.range(usize, 0, zd.scratchpad_size - 3));
    } else {
        buf = @ptrCast(ctx.allocBytes(3, 1));
    }
    buf[0] = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
    buf[1] = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
    buf[2] = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const a = @intFromPtr(args[0]);
    return if (a >= zd.scratchpad and a < zd.scratchpad + zd.scratchpad_size) 1 else 0;
}
