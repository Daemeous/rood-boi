//! zig_diff spec: func_800E7660 -> alloc_detour_record.allocDetourRecord (D2: pc/src/battle_movement.c).
//! Inputs: the unused state argument, null or a block of random bytes. The
//! heap allocator vs_main_allocHeap is a stub handing out a 32-byte arena
//! block of random bytes; func_800E4288 is a stub that zeroes the block as
//! the real one does, so the halfword stored at 0xC and the argument sizes
//! are compared, and D_800F5920 is the block's address.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").allocDetourRecord));

pub const classes = [_][:0]const u8{ "no-state", "state" };

pub const stubs = struct {
    pub fn vs_main_allocHeap(ctx: *zd.Ctx, size: anytype) ?*anyopaque {
        const n: usize = @intCast(size);
        const block = ctx.allocBytes(n, 16);
        for (block[0..n]) |*byte| byte.* = ctx.value(u8);
        return @ptrCast(block);
    }

    pub fn func_800E4288(ctx: *zd.Ctx, record: anytype, size: anytype) void {
        _ = ctx;
        const raw: ?*anyopaque = record;
        const bytes: [*]u8 = @ptrCast(raw.?);
        @memset(bytes[0..@intCast(size)], 0);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    if (ctx.chance(2)) return .{null};
    const state = ctx.allocBytes(0x40, 16);
    for (state[0..0x40]) |*byte| byte.* = ctx.int(u8);
    return .{@ptrCast(state)};
}

pub fn classify(args: Args) usize {
    return @intFromBool(args[0] != null);
}
