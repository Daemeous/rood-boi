//! zig_diff spec: func_800C00E8 -> func_800C00E8.func_800C00E8 (573B8.c).
//! Inputs: `_evtFile` (an i16 global) at zero, small and large positive, and
//! negative values, so the sign extension of the `* 3` term and the
//! `+ 110000` offset are both checked; `arg0` any int; `arg1` null or an
//! arena address. The allocator stub returns null sometimes and otherwise a
//! valid arena slot whose fields it fills, and the enqueue stub writes the
//! slot's state some of the time. The C has no branches; its value edges are
//! the sign of `_evtFile`.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_573B8");
const c = @import("c_573B8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C00E8));

pub const classes = [_][:0]const u8{ "evt-negative", "evt-zero", "evt-positive" };

pub const stubs = struct {
    pub fn vs_main_allocateCdQueueSlot(ctx: *zd.Ctx, file: anytype) [*c]c.vs_main_CdQueueSlot {
        _ = file;
        if (ctx.value(u8) < 40) return null;
        const slot = ctx.alloc(v.vs_main_CdQueueSlot);
        slot.state = ctx.value(i16);
        slot.priority = ctx.value(i16);
        return @ptrCast(@alignCast(slot));
    }

    pub fn vs_main_cdEnqueue(ctx: *zd.Ctx, slot: anytype, vram: ?*anyopaque) void {
        _ = vram;
        if (slot == null) return;
        const s: *v.vs_main_CdQueueSlot = @ptrCast(@alignCast(slot));
        if (ctx.value(u1) == 1) s.state = ctx.value(i16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g._evtFile().* = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768, -10922, 10923 });
    const vram: ?*anyopaque = if (ctx.chance(4)) null else @ptrCast(ctx.alloc(u32));
    return .{ ctx.int(c_int), vram };
}

pub fn classify(args: Args) usize {
    _ = args;
    const e = v.g._evtFile().*;
    if (e < 0) return 0;
    if (e == 0) return 1;
    return 2;
}
