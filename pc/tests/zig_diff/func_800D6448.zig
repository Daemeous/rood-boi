//! zig_diff spec: func_800D6448 -> func_800D6448.func_800D6448 (5BF94.c).
//! Inputs: a random script in a 64 KiB arena block (an index up to 255
//! reaches past unk1C's 16 entries: the C does not check), the index 0, 15,
//! 16 or 255, the value at its edges (the / 10 and % 10 split). The three
//! callees are recording stubs: their order is compared.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D6448));

pub const classes = [_][:0]const u8{ "in-unk1C", "past-unk1C" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const s: *v.D_800F53B8_t = @ptrCast(@alignCast(ctx.allocBytes(0x10000, 16)));
    return .{ s, ctx.pick(u8, &.{ 0, 1, 15, 16, 255 }), ctx.pick(u8, &.{ 0, 9, 10, 99, 100, 255 }) };
}

pub fn classify(args: Args) usize {
    return if (args[1] < 16) 0 else 1;
}
