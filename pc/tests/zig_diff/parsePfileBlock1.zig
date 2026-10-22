//! zig_diff spec: _parsePfileBlock1 -> parse_pfile_block1.parsePfileBlock1 (6F014.c).
//! Inputs: a pFileBlock1 in the arena with a 16-entry offset table after its
//! header and a 1 KB pad below it, so negative table offsets stay in memory the
//! harness owns. tableCount runs from 0 to 8: no tables, one to four tables,
//! and five to eight, which overflow block1Tables into block1Data and
//! block2Count. The count stays non-negative and small, so a mutated loop
//! bound never walks past the offsets the spec fills. dataOffset is a random
//! 16-bit value with edges, and table offsets fall around -512..60000. The
//! D_800F569C global points at an arena object whose stale block1TableCount
//! is set to a small value, so a dropped count store leaves a different count
//! behind.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_6F014");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").parsePfileBlock1));

pub const classes = [_][:0]const u8{ "no-tables", "one-to-four", "overflow" };

pub fn prepare(ctx: *zd.Ctx) Args {
    // Room below the block for negative table offsets.
    _ = ctx.allocBytes(1024, 16);
    const raw = ctx.allocBytes(4 + 4 * 16, 16);
    const block: *v.pFileBlock1 = @ptrCast(@alignCast(raw));

    const shape = ctx.range(u8, 0, 3);
    block.tableCount = switch (shape) {
        0 => 0,
        1, 2 => ctx.range(i16, 1, 4),
        else => ctx.range(i16, 5, 8),
    };
    block.dataOffset = ctx.pick(u16, &.{ 0, 1, 0x8000, 0xFFFF });

    // tableOffsets is declared [0]i32: entries 0..15 sit at byte 4 of the block.
    const offs: [*]i32 = @ptrCast(@alignCast(@as([*]u8, @ptrCast(block)) + 4));
    for (0..16) |k| {
        offs[k] = if (ctx.chance(4)) -512 else ctx.range(i32, -512, 60000);
    }

    // The global table pointer the C reads: an arena object for it. Its stale
    // block1TableCount is small, so the loop a mutation runs stays in bounds.
    const d = ctx.alloc(v.D_800F569C_t);
    d.block1TableCount = ctx.range(i32, 0, 8);
    v.g.D_800F569C().* = d;

    return .{block};
}

pub fn classify(args: Args) usize {
    const n = args[0].?.tableCount;
    if (n <= 0) return 0;
    if (n <= 4) return 1;
    return 2;
}
