//! zig_diff spec: func_800D4FB4 -> func_800D4FB4.func_800D4FB4 (5BF94.c).
//! Inputs: a D_800F53B8_t in the arena whose unkD1C.unk18.unk2 selects a row of
//! D_800EC330 (indices 0-6, the table's seven entries; the C reads no further),
//! and a D_800F569C_t whose block5Data points at an arena block with room for
//! the two curve entries. The curve table and the block's bytes are random, so
//! every copied byte is compared. Classes: the first row, the last row, and the
//! rows between them.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
const psx = @import("common").psx;

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D4FB4));

pub const classes = [_][:0]const u8{ "first-curve", "last-curve", "middle-curve" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0 = ctx.alloc(v.D_800F53B8_t);

    // pFileBlock5 is a header followed by two func_800FA098_arg0 entries
    // (unk4 is a [0] array): allocate room for both.
    const block5: *v.pFileBlock5 = @ptrCast(@alignCast(
        ctx.allocBytes(@sizeOf(v.pFileBlock5) + 2 * @sizeOf(v.func_800FA098_arg0), 16),
    ));
    block5.count = 2;

    const d = ctx.alloc(v.D_800F569C_t);
    psx.put(&d.block5Data, block5);
    v.g.D_800F569C().* = d;

    arg0.unkD1C.unk18.unk2 = ctx.edge(u16, &.{ 0, 1, 2, 3, 4, 5, 6 });

    return .{arg0};
}

pub fn classify(args: Args) usize {
    const idx = args[0].?.unkD1C.unk18.unk2;
    if (idx == 0) return 0;
    if (idx == 6) return 1;
    return 2;
}
