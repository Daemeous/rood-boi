//! zig_diff spec: _parsePfileBlock10 -> parse_pfile_block10.parsePfileBlock10 (6F014.c).
//! Inputs: a valid D_800F569C_t in the arena, set as the global pointer the C
//! reads, and an arg0 that is null, a low address, or a high 32-bit address
//! (never dereferenced, so any value is safe). The C stores arg0's guest
//! address into block10Data.raw, so every run compares that store.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_6F014");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").parsePfileBlock10));

pub const classes = [_][:0]const u8{ "null", "low", "high" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F569C_t);
    v.g.D_800F569C().* = obj;
    const p: ?*anyopaque = blk: {
        if (ctx.chance(5)) break :blk null;
        if (ctx.chance(4)) break :blk @ptrCast(obj);
        const a = ctx.pick(u32, &.{ 0x1, 0x7FFF_FFFF, 0x8000_0000, 0xFFFF_FFFF, 0x8000_1234 });
        break :blk @ptrFromInt(a);
    };
    return .{p};
}

pub fn classify(args: Args) usize {
    const p = args[0] orelse return 0;
    return if (@intFromPtr(p) < 0x8000_0000) 1 else 2;
}
