//! zig_diff spec: func_800BB5AC -> func_800BB5AC.func_800BB5AC (4A0A8.c).
//! Inputs: arg0 is an arena buffer (the C only offsets it and passes it on, so
//! the call trace compares the address), arg1 is any short (the C never reads
//! it). The heap block the C asks for is an arena block set by prepare, or NULL
//! one time in four (the C stores it without a test). The three callees are
//! recording stubs: the getShort stub returns any i16, the allocator returns
//! the prepared block, and the consumer stub writes the block's first byte and,
//! sometimes, _evtFile and D_800F4C54, so a port that reads them back at a
//! different point fails. The C has no branches or comparisons, so the classes
//! only split on the heap pointer.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BB5AC));

pub const classes = [_][:0]const u8{ "heap-null", "heap-valid" };

/// The block vs_main_allocHeapR hands out for this iteration (set by prepare).
var heap_block: ?*anyopaque = null;

pub const stubs = struct {
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) i16 {
        _ = p;
        return ctx.value(i16);
    }

    pub fn vs_main_allocHeapR(ctx: *zd.Ctx, size: anytype) ?*anyopaque {
        _ = ctx;
        _ = size;
        return heap_block;
    }

    pub fn func_800C00E8(ctx: *zd.Ctx, a: anytype, p: anytype) void {
        _ = a;
        // The consumer writes the block it was given (when there is one) and
        // may change the globals the C wrote, so their values after the call
        // are not the ones the C stored.
        const b: ?[*]u8 = @ptrCast(@alignCast(p));
        if (b) |bb| bb[0] = ctx.value(u8);
        if (ctx.value(u1) == 1) v.g._evtFile().* = ctx.value(i16);
        if (ctx.value(u1) == 1) v.g.D_800F4C54().* = ctx.value(i16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    heap_block = if (ctx.chance(4)) null else @ptrCast(ctx.allocBytes(0x1800, 16));
    const buf = ctx.allocBytes(16, 16);
    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (heap_block == null) 0 else 1;
}
