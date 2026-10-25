//! zig_diff spec: func_8009BE5C -> func_8009BE5C.func_8009BE5C (30DB0.c).
//! Inputs: a vs_battle_objectData in the arena with index 0..16 and load state
//! 0, 1, 2, or a state outside the switch (3, 4, 255) that falls past both
//! cases. Four actors are shared into the 17 D_800F4538 slots (some null);
//! the object's own slot always holds one. The actorId is always 0..5, so the
//! second switch always matches a case and never reads an uninitialised
//! `sectors`. State 0 covers every actorId 0..5 with the shared-sequence search
//! (index 0 or not, hits and misses), model ids zero, negative and small, and
//! the sector/lba tables forced to zero or not. State 1 covers the CD wait
//! with the slot done or busy; state 2 covers the parse with the heap-free flag
//! set (both signs) or clear. Stubs return null heap blocks sometimes, -1 from
//! _loadSeq sometimes, and the CD slot state that the enqueue (and the wait)
//! reads back.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009BE5C));

pub const classes = [_][:0]const u8{ "parse", "wait-busy", "wait-done", "load-actor0", "load-other", "state-outside-switch" };

fn maybePtr(ctx: *zd.Ctx, present: bool) ?*anyopaque {
    if (!present) return null;
    return @ptrCast(ctx.allocBytes(8, 8));
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const object = ctx.alloc(v.vs_battle_objectData);

    // Four actors; pool[0] is the one the object's index names.
    var pool: [4]*v.D_800F4538_t = undefined;
    for (&pool) |*slot| slot.* = ctx.alloc(v.D_800F4538_t);
    for (pool) |a| {
        a.unk6E6 = ctx.edge(u16, &.{ 1, 2, 3 });
        a.unk5B1 = ctx.edge(u8, &.{ 0, 1, 2, 3, 255 });
        psx.put(&a.unk5D4, maybePtr(ctx, ctx.chance(2)));
    }
    pool[0].unk6E6 = ctx.edge(u16, &.{ 1, 2, 2, 3 });

    // The index and the 17-slot table: the object's slot always set.
    const idx = ctx.range(usize, 0, 16);
    object.index = @intCast(idx);
    const table = v.g.D_800F4538();
    for (table, 0..) |*e, i| {
        e.* = if (i != idx and ctx.chance(4)) null else pool[ctx.range(usize, 0, 3)];
    }
    table[idx] = pool[0];

    // Globals the C reads on every path. State 3, 4 and 255 match no case of
    // the first switch: they fall straight to the second one.
    const state = ctx.edge(u8, &.{ 0, 0, 0, 1, 2, 3, 4, 255 });
    v.g._loadShpState().* = state;
    v.g.D_800F22D0().* = ctx.edge(i32, &.{ 0, 1, -1, 0x40000, -0x7FFFFFFF });
    const slot = ctx.alloc(v.vs_main_CdQueueSlot);
    slot.state = if (ctx.chance(2)) 4 else ctx.edge(i16, &.{ 0, 1, 2, 3, 5, -1 });
    v.g.D_800F22E0().* = slot;

    object.dataType = ctx.int(u8);
    object.dataAddr = ctx.int(u32);
    object.variant = ctx.int(u8);
    object.material = ctx.int(u8);
    object.modelId = ctx.int(i16);

    // Every state that reaches the second switch needs a case that matches:
    // actorId is always 0..5 (no uninitialised `sectors` path).
    const actorId = ctx.edge(u8, &.{ 0, 0, 0, 0, 1, 2, 3, 4, 5 });
    object.actorId = actorId;
    switch (actorId) {
        0 => object.modelId = ctx.edge(i16, &.{ -1, 0, 0, 1, 2, 3, 40 }),
        1, 2 => {
            const m = ctx.edge(i16, &.{ 0, 1, 7, 110, 111 });
            object.modelId = m;
            v.g.D_800E8B40()[@intCast(m)] = if (ctx.chance(3)) 0 else ctx.edge(u8, &.{ 1, 31, 32, 255 });
        },
        3 => {
            object.material = ctx.edge(u8, &.{ 0, 1, 2, 3, 4 });
        },
        4 => {
            const mat = ctx.range(u8, 0, 9);
            const mid = ctx.range(i16, 0, 3);
            object.material = mat;
            object.modelId = mid;
            v.g.D_800E8EFC()[mat][@intCast(mid)] = if (ctx.chance(3)) 0 else ctx.edge(u8, &.{ 1, 2, 255 });
        },
        5 => {
            object.modelId = ctx.edge(i16, &.{ 0, 1, 2, 3, 4 });
            object.material = ctx.range(u8, 0, 9);
        },
        else => unreachable,
    }

    return .{object};
}

pub fn classify(args: Args) usize {
    const st = v.g._loadShpState().*;
    if (st >= 3) return 5;
    if (st == 2) return 0;
    if (st == 1) return if (v.g.D_800F22E0().*.?.state == 4) 2 else 1;
    return if (args[0].?.actorId == 0) 3 else 4;
}

pub const stubs = struct {
    pub fn vs_main_allocHeapR(ctx: *zd.Ctx, size: anytype) ?*anyopaque {
        _ = size;
        if (ctx.value(u8) < 40) return null;
        return @ptrCast(ctx.allocBytes(8, 8));
    }

    pub fn vs_main_allocHeap(ctx: *zd.Ctx, size: anytype) ?*anyopaque {
        _ = size;
        if (ctx.value(u8) < 40) return null;
        return @ptrCast(ctx.allocBytes(8, 8));
    }

    // The C reads the slot's state after the enqueue (below) and the wait.
    pub fn vs_main_allocateCdQueueSlot(ctx: *zd.Ctx, file: anytype) [*c]v.vs_main_CdQueueSlot {
        _ = file;
        return ctx.alloc(v.vs_main_CdQueueSlot);
    }

    // The real queue moves the slot: its state is what the wait tests.
    pub fn vs_main_cdEnqueue(ctx: *zd.Ctx, slot: anytype, vram: anytype) void {
        _ = vram;
        const s: *v.vs_main_CdQueueSlot = @ptrCast(@alignCast(slot));
        s.state = if (ctx.value(u1) == 1) 4 else ctx.value(i16);
    }

    // Writes the object it loads for, and returns -1 (the C's test) or a result.
    pub fn _loadSeq(ctx: *zd.Ctx, object: anytype) c_int {
        const o: *v.vs_battle_objectData = @ptrCast(@alignCast(if (comptime @typeInfo(@TypeOf(object)) == .optional) object.? else object));
        o.variant = ctx.value(u8);
        if (ctx.value(u1) == 1) return -1;
        return ctx.value(c_int);
    }
};
