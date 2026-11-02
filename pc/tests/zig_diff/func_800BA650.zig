//! zig_diff spec: func_800BA650 -> func_800BA650.func_800BA650 (4A0A8.c).
//! The inputs are a 4-byte buffer in the arena (byte 1 is the signed read, byte 2
//! the unsigned read, byte 3 is past the C's reads and must still match) with
//! sign-bit and zero edges, the global D_800F4C69 at 0, 0x80 or 0xFF, and arg1 at
//! its edges. The callee may change D_800F4C69, and it returns any int.
//! The C has no branches, so the classes split on the sign bit of arg0[1].
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BA650));

pub const classes = [_][:0]const u8{ "byte1-small", "byte1-sign" };

pub const stubs = struct {
    pub fn func_80045574(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype) c_int {
        _ = a;
        _ = b;
        _ = c;
        // The real callee may change the global; the C does not read it back.
        if (ctx.value(u1) == 1) v.g.D_800F4C69().* = ctx.value(u8);
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(4, 1);
    buf[0] = ctx.int(u8);
    buf[1] = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
    buf[2] = ctx.pick(u8, &.{ 0, 0x7F, 0x80, 0xFF });
    buf[3] = ctx.int(u8);
    v.g.D_800F4C69().* = ctx.pick(u8, &.{ 0, 0x80, 0xFF });
    const arg1: c_short = ctx.pick(c_short, &.{ 0, -1, 0x7FFF, -32768 });
    return .{ buf, arg1 };
}

pub fn classify(args: Args) usize {
    return if (args[0][1] >= 0x80) 1 else 0;
}
