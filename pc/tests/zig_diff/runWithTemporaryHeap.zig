//! zig_diff spec: func_800E6B4C -> pc.temporary_heap_pass.runWithTemporaryHeap (D2: pc/src/navigation.c).
//! No inputs: the stub of vs_main_allocHeapR returns an arena block or null
//! (a quarter of the runs, chosen by prepare so both runs agree); with null the
//! C returns at once, otherwise func_800E6098 runs and the block is freed
//! (the stub of vs_main_freeHeapR records the pointer).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").runWithTemporaryHeap));

pub const classes = [_][:0]const u8{ "alloc-fails", "alloc-ok" };

var block: ?*anyopaque = null;

pub fn prepare(ctx: *zd.Ctx) Args {
    block = if (ctx.chance(4)) null else @ptrCast(ctx.allocBytes(0x40, 16));
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return @intFromBool(block != null);
}

pub const stubs = struct {
    pub fn vs_main_allocHeapR(ctx: *zd.Ctx, size: anytype) ?*anyopaque {
        _ = ctx;
        _ = size;
        return block;
    }

    pub fn func_800E6098(ctx: *zd.Ctx) void {
        _ = ctx;
    }

    pub fn vs_main_freeHeapR(ctx: *zd.Ctx, p: anytype) void {
        _ = ctx;
        _ = p;
    }
};
