//! zig_diff spec: func_80091378 -> func_80091378.func_80091378 (2842C.c).
//! Inputs: the heap pointer D_800F1DD4 is null or not (nonnull values are
//! arena blocks and arbitrary addresses the free stub never dereferences),
//! and D_800F1DC0/D_800F1DC2 start as any i16 (0, 1, -1, extremes, random).
//! The C has no ordered comparison: its only test is the pointer null test.
//! vs_main_freeHeap is a recording stub that changes nothing the C reads
//! back; the heap bytes it would manage are random and compared anyway.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80091378));

pub const classes = [_][:0]const u8{ "heap-null", "heap-held" };

pub const stubs = struct {
    pub fn vs_main_freeHeap(ctx: *zd.Ctx, p: anytype) void {
        _ = ctx;
        _ = p;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.alloc(u64);
    const p: ?*anyopaque = if (ctx.chance(2))
        null
    else if (ctx.chance(4))
        @ptrFromInt(@as(usize, ctx.value(u32) | 1))
    else
        @ptrCast(block);
    v.g.D_800F1DD4().* = p;
    v.g.D_800F1DC0().* = ctx.pick(i16, &.{ 0, 1, -1, 0x7fff, -32768 });
    v.g.D_800F1DC2().* = ctx.pick(i16, &.{ 0, 1, -1, 0x7fff, -32768 });
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (v.g.D_800F1DD4().* == null) 0 else 1;
}
