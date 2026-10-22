//! zig_diff spec: _parsePfileCurveBlock -> parse_pfile_curve_block.parsePfileCurveBlock (6F014.c).
//! Inputs: a pFileCurveBlock in the arena with count 0, 1, 2-32 or past 32
//! (up to 150, so the C's unchecked `curves[i]` stores run past the array
//! into the arena struct, which is allocated large enough for them), a
//! dataOffset that is small, 0x7FFF, 0x8000 or 0xFFFF (so the signed/unsigned
//! read differs), and curveSizes drawn from 0, 1, 127, 128 and 255. The
//! D_800F569C global points at an arena struct. The C calls nothing, so
//! there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_6F014");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").parsePfileCurveBlock));

pub const classes = [_][:0]const u8{ "count-0", "count-1", "count-2-to-32", "count-past-32" };

pub fn prepare(ctx: *zd.Ctx) Args {
    // Sized for curves[0..150] (the C stores past curves[31] without a check).
    const data: *v.D_800F569C_t = @ptrCast(@alignCast(ctx.allocBytes(2048, 16)));
    v.g.D_800F569C().* = data;

    const block: *v.pFileCurveBlock = @ptrCast(@alignCast(ctx.allocBytes(1024, 16)));
    const edges = [_]u16{ 0, 1, 2, 31, 32, 33, 150 };
    block.count = if (ctx.chance(2)) edges[ctx.range(usize, 0, edges.len - 1)] else ctx.range(u16, 0, 150);
    block.dataOffset = ctx.pick(u16, &.{ 0, 4, 8, 0x7FFF, 0x8000, 0xFFFF });

    // curveSizes is [0]u8: its bytes are read at offset 4 + i.
    var i: usize = 0;
    while (i < block.count) : (i += 1) {
        const off: isize = @intCast(4 + i);
        psx.byteAt(u8, block, off).* = ctx.pick(u8, &.{ 0, 1, 127, 128, 255 });
    }
    return .{block};
}

pub fn classify(args: Args) usize {
    const n = args[0].?.count;
    if (n == 0) return 0;
    if (n == 1) return 1;
    return if (n <= 32) 2 else 3;
}
