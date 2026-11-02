//! zig_diff spec: _loadScreff2 -> load_screff2.loadScreff2 (5BF94.c).
//! Inputs: load 1 or 0; D_800EB9B4 an arena record whose slot is an arena
//! queue slot in state 4 (read: freed) or another. vs_main_allocateCdQueueSlot
//! gets the C's stack file (a tag): its stub logs the lba and size into an
//! arena record and returns an arena slot.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").loadScreff2));

pub const classes = [_][:0]const u8{ "load", "done", "pending" };

var file_log: ?*v.vs_main_CdFile = null;
var slot: ?*v.vs_main_CdQueueSlot = null;

pub const stubs = struct {
    pub fn vs_main_allocateCdQueueSlot(ctx: *zd.Ctx, file: anytype) ?*v.vs_main_CdQueueSlot {
        _ = ctx;
        file_log.?.* = @as(*const v.vs_main_CdFile, @ptrCast(@alignCast(file))).*;
        return slot;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    file_log = ctx.alloc(v.vs_main_CdFile);
    slot = ctx.alloc(v.vs_main_CdQueueSlot);
    const p = ctx.alloc(v.D_800EB9B4_t);
    const old = ctx.alloc(v.vs_main_CdQueueSlot);
    old.state = ctx.pick(i16, &.{ 4, 4, 3, 5 });
    psx.put(&p.slot, old);
    v.g.D_800EB9B4().* = p;
    return .{ctx.pick(c_int, &.{ 0, 0, 1, -1 })};
}

pub fn classify(args: Args) usize {
    if (args[0] != 0) return 0;
    return if (psx.get(v.g.D_800EB9B4().*.?.slot).?.state == 4) 1 else 2;
}
