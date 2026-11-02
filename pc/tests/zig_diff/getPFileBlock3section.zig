//! zig_diff spec: _getPFileBlock3section -> get_pfile_block3section.getPFileBlock3section (5BF94.c).
//! Inputs: an arena block with a count and 32 random u16 offsets (some
//! above 0x7FFF: they zero-extend), index 0..31 (the C does not check it
//! against the count). The result is compared as an address.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").getPFileBlock3section));

pub const classes = [_][:0]const u8{ "first", "later", "high-offset" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const block: *v.pFileBlock3 = @ptrCast(@alignCast(ctx.allocBytes(4 + 64, 16)));
    const offsets: [*]u16 = @ptrCast(&block.offsets);
    for (0..32) |i| offsets[i] = ctx.pick(u16, &.{ 0, 4, 0x7FFF, 0x8000, 0xFFFF });
    return .{ block, ctx.pick(c_int, &.{ 0, 1, 31 }) & 31 };
}

pub fn classify(args: Args) usize {
    const offsets: [*]u16 = @ptrCast(&args[0].?.offsets);
    if (offsets[@intCast(args[1])] >= 0x8000) return 2;
    return if (args[1] == 0) 0 else 1;
}
