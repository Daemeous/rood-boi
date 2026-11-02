//! zig_diff spec: func_800B9F28 -> func_800B9F28.func_800B9F28 (4A0A8.c).
//! The C reads arg0[1] and passes it to func_8008EAF8, then returns 0. The
//! inputs are a 4-byte arena buffer whose byte 1 takes the edge values 0, 1,
//! 127, 128 and 255 (so the sign bit of the u_char is set and clear), and a
//! random short. The callee is a void recording stub that writes nothing:
//! the C never reads anything back after the call, so no stub behaviour is
//! needed. Nothing in the C is a branch, so the classes only split the
//! byte's sign bit.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B9F28));

pub const classes = [_][:0]const u8{ "byte-sign-clear", "byte-sign-set" };

pub fn prepare(ctx: *zd.Ctx) Args {
    // Byte 0 stays random; the C only reads byte 1 of the buffer.
    const buf = ctx.allocBytes(4, 1);
    buf[1] = ctx.pick(u8, &.{ 0, 1, 127, 128, 255 });
    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    return if ((args[0][1] & 0x80) == 0) 0 else 1;
}
