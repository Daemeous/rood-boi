//! zig_diff spec: charge_skip_kind -> charge_skip.kind (146C.c, charge_skip.inc).
//! Inputs: an action index at each range edge (0x8C/0x8D, 0xB7/0xB8, 0xDF/0xE0), negative and extreme.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").kind));

pub const classes = [_][:0]const u8{ "spell", "enemy-art", "break-art", "item" };

pub fn prepare(ctx: *zd.Ctx) Args {
    return .{ctx.pick(c_int, &.{ 0, 0x8C, 0x8D, 0xB7, 0xB8, 0xDF, 0xE0, 0xFF, -1, std.math.minInt(c_int), std.math.maxInt(c_int) })};
}

pub fn classify(args: Args) usize {
    const i = args[0];
    return if (i < 0x8D) 0 else if (i < 0xB8) 1 else if (i < 0xE0) 2 else 3;
}
