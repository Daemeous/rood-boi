//! zig_diff spec: func_800C031C -> func_800C031C.func_800C031C (573B8.c).
//! Inputs: the edge-table global D_800EB9B8 is either NULL (the C allocates a
//! 0x4B1C block; the allocator stub hands out a fixed block at the end of the
//! arena) or points at a block at the start of the arena. The C has no other
//! inputs: the memzero stub clears the block, then the 720 edge shorts are
//! written at a fixed offset. Every loop runs to its fixed bound, so every
//! branch is taken in both classes.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_573B8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C031C));

const block_size = 0x4B1C;
const alloc_off: usize = (zd.arena_size - block_size) & ~@as(usize, 15);
const existing_off: usize = 0;

pub const classes = [_][:0]const u8{ "null-global", "existing-block" };

pub const stubs = struct {
    pub fn vs_main_allocHeap(ctx: *zd.Ctx, size: anytype) ?*anyopaque {
        _ = ctx;
        _ = size;
        return @ptrCast(&zd.zdiff_arena[alloc_off]);
    }

    pub fn vs_battle_rMemzero(ctx: *zd.Ctx, p: anytype, n: anytype) void {
        _ = ctx;
        const bytes: [*]u8 = @ptrCast(@alignCast(p));
        @memset(bytes[0..@as(usize, @intCast(n))], 0);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const g = v.g.D_800EB9B8();
    if (ctx.chance(2)) {
        g.* = null;
    } else {
        g.* = @ptrCast(@alignCast(&zd.zdiff_arena[existing_off]));
    }
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (v.g.D_800EB9B8().* == null) 0 else 1;
}
