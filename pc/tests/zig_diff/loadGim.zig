//! zig_diff spec: vs_battle_loadGim -> load_gim.loadGim (5BF94.c).
//! Inputs: D_800EB9BC null (a load) or set; the GIM id 0..3, its packed
//! lba-and-size word in _gimLbas random; the palette any (narrowed).
//! vs_main_allocHeapR returns arena blocks (the image buffer is only
//! addressed); vs_main_allocateCdQueueSlot gets the C's stack file (a tag):
//! its stub logs it and returns an arena slot.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").loadGim));

pub const classes = [_][:0]const u8{ "loaded", "load" };

var file_log: ?*v.vs_main_CdFile = null;
var slot: ?*v.vs_main_CdQueueSlot = null;

pub const stubs = struct {
    pub fn vs_main_allocHeapR(ctx: *zd.Ctx, size: anytype) ?*anyopaque {
        if (size > 4096) return @ptrCast(ctx.allocBytes(16, 16));
        return @ptrCast(ctx.allocBytes(size, 16));
    }
    pub fn vs_main_allocateCdQueueSlot(ctx: *zd.Ctx, file: anytype) ?*v.vs_main_CdQueueSlot {
        _ = ctx;
        file_log.?.* = @as(*const v.vs_main_CdFile, @ptrCast(@alignCast(file))).*;
        return slot;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    file_log = ctx.alloc(v.vs_main_CdFile);
    slot = ctx.alloc(v.vs_main_CdQueueSlot);
    v.g.D_800EB9BC().* = if (ctx.chance(4)) ctx.alloc(v.gim_t) else null;
    return .{ ctx.range(c_int, 0, 3), ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (v.g.D_800EB9BC().* == null) 1 else 0;
}
