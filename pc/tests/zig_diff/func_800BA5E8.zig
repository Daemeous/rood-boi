//! zig_diff spec: func_800BA5E8 -> func_800BA5E8.func_800BA5E8 (4A0A8.c).
//! Inputs: a pointer to 4 arena bytes whose random contents cover every byte
//! value, with and without the sign bit (a quarter of the runs clear bit 7 of
//! bytes 1-3), an edge-valued short arg1 (the C never reads it), and a random
//! D_800F4C69 before the call. The callee func_80045000 is a recording stub
//! with the default int return; the C discards that value and reads nothing
//! back from the callee, so no callee model is needed.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BA5E8));

pub const classes = [_][:0]const u8{ "sign-bit-set", "no-sign-bit" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const bytes: [*c]u8 = @ptrCast(ctx.allocBytes(4, 1));
    if (ctx.chance(4)) {
        bytes[1] &= 0x7F;
        bytes[2] &= 0x7F;
        bytes[3] &= 0x7F;
    }
    v.g.D_800F4C69().* = ctx.int(u8);
    return .{ bytes, ctx.pick(c_short, &.{ 0, -1, 1, 32767, -32768 }) };
}

pub fn classify(args: Args) usize {
    const p = args[0];
    return if (((p[1] | p[2] | p[3]) & 0x80) != 0) 0 else 1;
}
