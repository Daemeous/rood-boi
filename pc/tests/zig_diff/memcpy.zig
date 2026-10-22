//! zig_diff spec: vs_battle_memcpy -> memcpy.memcpy (573B8.c).
//! Inputs: a copy of 8 to 192 bytes (one group of 8 in half the runs, up to
//! 24 groups otherwise) from an arena block into a second block, or into a
//! block overlapping the source by 2 to 16 bytes. Sizes that are not
//! positive multiples of 8 are never generated: the C's do-loop only stops
//! when from (src + 8k) equals end (src + numBytes), so for those sizes it
//! walks off mapped memory. The bad-size branch of the C is waived in
//! specs.toml for that reason.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").memcpy));

pub const classes = [_][:0]const u8{ "copy-8", "copy-multi", "overlap" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const k: u32 = if (ctx.chance(2)) 1 else ctx.range(u32, 1, 24);
    const n: c_int = @intCast(8 * k);
    const size: usize = @as(usize, @intCast(n)) + 64;
    const src_block = ctx.allocBytes(size, 16);
    const overlap = ctx.chance(4);
    const dst_block: [*]u8 = if (overlap)
        src_block + 2 * ctx.range(usize, 1, 8)
    else
        ctx.allocBytes(size, 16);
    const dst: ?*anyopaque = @ptrCast(dst_block);
    const src: ?*const anyopaque = @ptrCast(src_block);
    return .{ dst, src, n };
}

pub fn classify(args: Args) usize {
    const dst = @intFromPtr(args[0].?);
    const src = @intFromPtr(args[1].?);
    const n: usize = @intCast(args[2]);
    if (dst > src and dst < src + n + 64) return 2;
    return if (n == 8) 0 else 1;
}
