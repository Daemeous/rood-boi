//! zig_diff spec: func_800BBDDC -> func_800BBDDC.func_800BBDDC (4A0A8.c).
//! Inputs: the heap block pointer D_800F4BA4 is either null or a block the
//! arena owns. The C has no branches and no comparisons, so the only input
//! that matters is the pointer it passes on. The free stub models the heap
//! writing a header into the block it frees, so a port that frees the wrong
//! block or skips the call leaves different bytes behind.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BBDDC));

pub const classes = [_][:0]const u8{ "null-block", "live-block" };

pub const stubs = struct {
    /// The heap writes a header word into a block it frees (non-null only).
    pub fn vs_main_freeHeapR(ctx: *zd.Ctx, p: anytype) void {
        const q: ?*u32 = @ptrCast(@alignCast(p));
        if (q) |h| h.* = ctx.value(u32);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const slot = v.g.D_800F4BA4();
    if (ctx.chance(4)) {
        slot.* = null;
    } else {
        slot.* = ctx.alloc(v.D_800F4BA4_t);
    }
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (v.g.D_800F4BA4().* == null) 0 else 1;
}
