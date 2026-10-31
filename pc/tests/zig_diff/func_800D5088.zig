//! zig_diff spec: func_800D5088 -> func_800D5088.func_800D5088 (5BF94.c).
//! Inputs: a D_800F53B8_t in the arena whose unkD1C.unk18.unk2 picks a row of
//! D_800EC368 (the first row, the last row of the 16 the harness gives it, or a
//! middle row), and a block5 data block with room for all 256 element indices a
//! random table byte can select. The table keeps its random bytes, so each
//! stored curve and index is checked against whatever the table really holds.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D5088));

pub const classes = [_][:0]const u8{ "row-0", "row-middle", "row-15" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg = ctx.alloc(v.D_800F53B8_t);
    const score = ctx.alloc(v.D_800F569C_t);
    // pFileBlock5: a count, then 256 elements of 204 bytes (func_800FA098_arg0).
    const blk: *v.pFileBlock5 = @ptrCast(@alignCast(ctx.allocBytes(4 + 256 * 204, 16)));
    psx.put(&score.block5Data, blk);
    v.g.D_800F569C().* = score;
    // Table rows 0..15 only: the harness gives D_800EC368 no more room.
    arg.unkD1C.unk18.unk2 = if (ctx.chance(3)) 0 else if (ctx.chance(2)) 15 else ctx.range(u16, 1, 14);
    return .{arg};
}

pub fn classify(args: Args) usize {
    const row = args[0].?.unkD1C.unk18.unk2;
    return if (row == 0) 0 else if (row == 15) 2 else 1;
}
