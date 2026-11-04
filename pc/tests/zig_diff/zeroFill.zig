//! zig_diff spec: func_800E4288 -> pc.zero_fill.zeroFill (D2: pc/src/battle_helpers.c).
//! Inputs: a random-filled arena block and a destination at its start or
//! skewed by 1-3 bytes (unaligned), and a byte count that is under 4 (the
//! invalid span pc_unimplemented reports), exactly 4, a word multiple, or
//! one with a ragged tail (only whole words are cleared). The stub of
//! pc_unimplemented compares its text, so the report is checked, and both
//! sides carry on past it as the original does.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").zeroFill));

pub const classes = [_][:0]const u8{ "too-short", "unaligned", "aligned" };

pub fn textArgument(call: []const u8, index: usize) bool {
    return index == 0 and std.mem.eql(u8, call, "pc_unimplemented");
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.allocBytes(0x120, 16);
    for (block[0..0x120]) |*byte| byte.* = ctx.int(u8);
    const skew: usize = if (ctx.chance(3)) ctx.range(usize, 1, 3) else 0;
    const bytes = ctx.edge(c_uint, &.{ 0, 1, 2, 3, 4, 4, 4, 5, 6, 7, 8, 12, 0x10, 0x40, 0x41, 0x7f, 0x80 });
    return .{ block + skew, bytes };
}

pub fn classify(args: Args) usize {
    if (args[1] < 4) return 0;
    return if (@intFromPtr(args[0]) & 3 != 0) 1 else 2;
}
