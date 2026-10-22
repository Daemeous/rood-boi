//! zig_diff spec: _parsePfileBlock2 -> parse_pfile_block2.parsePfileBlock2 (6F014.c).
//! Inputs: a pFileBlock2 in the arena whose count (unk0) is any i32, with the
//! edge values 0, 1, -1, 65535, 65536, INT_MIN and INT_MAX half the time and
//! random bits otherwise, so negative and high-bit counts both occur. The
//! global D_800F569C is set to a fresh arena block, whose random bytes the C
//! overwrites in block2Count and block2Data. The block is never null: the C
//! reads block->unk0 unconditionally. The C has no calls and no comparisons,
//! so the classes only split on the sign of the count.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_6F014");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").parsePfileBlock2));

pub const classes = [_][:0]const u8{ "count-negative", "count-nonnegative" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.alloc(v.pFileBlock2);
    block.unk0 = ctx.pick(i32, &.{ 0, 1, -1, 65535, 65536, std.math.minInt(i32), std.math.maxInt(i32) });
    const obj = ctx.alloc(v.D_800F569C_t);
    v.g.D_800F569C().* = obj;
    return .{block};
}

pub fn classify(args: Args) usize {
    return if (args[0].?.unk0 < 0) 0 else 1;
}
