//! zig_diff spec: func_800C05B4 -> func_800C05B4.func_800C05B4 (573B8.c).
//! Inputs: the heap pointer D_800EB9B8 is either NULL (about half the runs)
//! or an arena block the harness owns. The vs_main_freeHeap stub overwrites
//! the first 8 bytes of the block it is given (a heap header the real free
//! rewrites) with ctx.value bytes, so a port that frees a stale or NULL
//! pointer, or that reads the block afterwards, disagrees with the C.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_573B8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C05B4));

pub const stubs = struct {
    pub fn vs_main_freeHeap(ctx: *zd.Ctx, p: anytype) void {
        const q = p orelse return;
        const bytes: [*]u8 = @ptrCast(@alignCast(q));
        for (0..8) |i| bytes[i] = ctx.value(u8);
    }
};

pub const classes = [_][:0]const u8{ "null-heap", "live-heap" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const slot = v.g.D_800EB9B8();
    if (ctx.chance(2)) {
        slot.* = null;
    } else {
        slot.* = ctx.alloc(v.D_800EB9B8_t);
    }
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (v.g.D_800EB9B8().* == null) 0 else 1;
}
