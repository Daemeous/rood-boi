//! zig_diff spec: func_800B97E4 -> func_800B97E4.func_800B97E4 (4A0A8.c).
//! Inputs: a 16-byte arena buffer whose byte 1 is random (with the sign bit
//! set or clear, and the edge bytes 0, 0x7F, 0x80, 0xFF sometimes), a random
//! prior value of D_800F19C8 (so a dropped store is visible), and arg1 from
//! the edges of c_short (the C never reads it). There are no branches and
//! no callees; the classes split on the sign bit of arg0[1], which is the
//! only value the C's store depends on.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B97E4));

pub const classes = [_][:0]const u8{ "sign-set", "sign-clear" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(16, 16);
    if (ctx.chance(4)) buf[1] = ctx.pick(u8, &.{ 0, 0x7F, 0x80, 0xFF });
    v.g.D_800F19C8().* = ctx.int(i32);
    const arg1 = ctx.pick(c_short, &.{ 0, 1, -1, 0x7FFF, -0x8000 });
    return .{ buf, arg1 };
}

pub fn classify(args: Args) usize {
    return if ((args[0][1] & 0x80) != 0) 0 else 1;
}
