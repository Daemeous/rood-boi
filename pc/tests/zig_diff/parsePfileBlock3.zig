//! zig_diff spec: _parsePfileBlock3 -> parse_pfile_block3.parsePfileBlock3 (6F014.c).
//! Inputs: the global D_800F569C points at a D_800F569C_t in the arena (its
//! block3Data field is random). The block argument is null, an arena address,
//! a PS1 low-RAM address (0x80000000, bit 31 set), a scratchpad address, or an
//! arbitrary 32-bit address (bit 31 set or clear). The C only stores the
//! address, so no pointed-to memory is touched. The C has no ordered
//! comparisons and no zero tests of values, and makes no calls.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_6F014");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").parsePfileBlock3));

pub const classes = [_][:0]const u8{ "null-block", "low-bit-clear", "high-bit-set" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F569C_t);
    v.g.D_800F569C().* = obj;

    const block: ?*anyopaque = switch (ctx.range(u8, 0, 4)) {
        0 => null,
        1 => @ptrCast(obj),
        2 => @ptrFromInt(@as(usize, ctx.pick(u32, &.{ 0x8000_0000, 0x1F80_0000, 0xFFFF_FFFF, 1, 0x7FFF_FFFF }))),
        3 => @ptrFromInt(@as(usize, ctx.pick(u32, &.{ 0, 0x8000_0000, 0x8000_0001 }))),
        else => @ptrFromInt(@as(usize, ctx.int(u32))),
    };
    return .{block};
}

pub fn classify(args: Args) usize {
    const p = args[0] orelse return 0;
    return if (@intFromPtr(p) & 0x8000_0000 != 0) 2 else 1;
}
