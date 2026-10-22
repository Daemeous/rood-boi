//! zig_diff spec: _parsePfileBlock8 -> parse_pfile_block8.parsePfileBlock8 (6F014.c).
//! Inputs: a fresh D_800F569C_t in the arena (set through the global, random
//! bytes elsewhere) and a block pointer that is null, an arena address, a
//! scratchpad address, a PS1 low-RAM address, or an arbitrary 32-bit address
//! value. Every address stays at or below 0xFFFF_FFFE, so the C's
//! `psx_addr(block)` and the mutated `(char*)block + 1` stay below 4 GB (psx_addr
//! is fatal above it). The C never dereferences the block, so the values need
//! not be mapped.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_6F014");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").parsePfileBlock8));

pub const classes = [_][:0]const u8{ "null", "arena", "scratchpad", "low-ram", "any-u32" };

pub fn prepare(ctx: *zd.Ctx) Args {
    // The C reads D_800F569C and dereferences it, so it must always point at an object.
    const d = ctx.alloc(v.D_800F569C_t);
    v.g.D_800F569C().* = d;

    const block: ?*anyopaque = switch (ctx.range(u8, 0, 4)) {
        0 => null,
        1 => @ptrCast(ctx.allocBytes(16, 16)),
        2 => @ptrFromInt(zd.scratchpad + ctx.range(usize, 0, zd.scratchpad_size - 1)),
        3 => @ptrFromInt(zd.low_ram + ctx.range(usize, 0, zd.low_ram_size - 1)),
        else => blk: {
            // Keep block + 1 (the P5 mutation) below 4 GB: 0xFFFF_FFFF maps to 0xFFFF_FFFE.
            const x = ctx.pick(u32, &.{ 0x8000_0000, 0x7FFF_FFFF, 0x1F80_0000, 0x00FF_FFFF, 0xFFFF_FFFE });
            break :blk @ptrFromInt(if (x == 0xFFFF_FFFF) @as(usize, 0xFFFF_FFFE) else x);
        },
    };
    return .{block};
}

pub fn classify(args: Args) usize {
    const a = @intFromPtr(args[0]);
    if (a == 0) return 0;
    const arena = @intFromPtr(&zd.zdiff_arena[0]);
    if (a >= arena and a < arena + zd.arena_size) return 1;
    if (a >= zd.scratchpad and a < zd.scratchpad + zd.scratchpad_size) return 2;
    if (a >= zd.low_ram and a < zd.low_ram + zd.low_ram_size) return 3;
    return 4;
}
