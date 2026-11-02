//! zig_diff spec: func_800D66FC -> func_800D66FC.func_800D66FC (5BF94.c).
//! Inputs: a random script in a 64 KiB arena block (the index up to 255
//! reaches past unk1C's 16 entries: the C does not check), the index 0, 15,
//! 16 or 255, frames 0 (made 1), 1 or 255; the translations at the int edges
//! (the * 4096 wraps).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D66FC));

pub const classes = [_][:0]const u8{ "zero-frames", "frames", "past-unk1C" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const s: *v.D_800F53B8_t = @ptrCast(@alignCast(ctx.allocBytes(0x10000, 16)));
    const index = ctx.pick(u8, &.{ 0, 1, 15, 16, 255 });
    const t = &psx.at(&s.unk1C, index).unk38.t;
    for (0..3) |i| t[i] = ctx.pick(i32, &.{ 0, 1, -1, 0x80000, std.math.maxInt(i32) });
    return .{ s, index, ctx.pick(u8, &.{ 0, 0, 1, 255 }) };
}

pub fn classify(args: Args) usize {
    if (args[1] >= 16) return 2;
    return if (args[2] == 0) 0 else 1;
}
