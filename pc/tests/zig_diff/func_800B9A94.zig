//! zig_diff spec: func_800B9A94 -> func_800B9A94.func_800B9A94 (4A0A8.c).
//! Inputs: a 4-byte block in the arena whose bytes 1, 2 and 3 are the
//! callee's arguments. Each byte takes an edge (0, 0x7F, 0x80, 0xFF) half the
//! time and any value otherwise, so byte 3 sees the sign bit both set and
//! clear, which matters for the `arg0[3] * 0x10` promotion. The callee is a
//! void recording stub that writes nothing, so the return value is the only
//! result the C produces beyond the call trace.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B9A94));

pub const classes = [_][:0]const u8{ "sign-bit", "no-sign-bit" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const p: [*c]u8 = ctx.allocBytes(4, 1);
    p[0] = ctx.int(u8);
    p[1] = ctx.pick(u8, &.{ 0, 0x7F, 0x80, 0xFF });
    p[2] = ctx.pick(u8, &.{ 0, 0x7F, 0x80, 0xFF });
    p[3] = ctx.pick(u8, &.{ 0, 0x01, 0x7F, 0x80, 0xFF });
    return .{p};
}

pub fn classify(args: Args) usize {
    return if (args[0][3] & 0x80 != 0) 0 else 1;
}
