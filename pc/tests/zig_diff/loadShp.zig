//! zig_diff spec: _loadShp -> load_shp.loadShp (30DB0.c).
//! The inputs cover the three loader states and the default state, which
//! falls through into state 1. State 0 runs the match loop over D_800F4538
//! with no match, one match at any entry, null entries, and a u16 that
//! matches only in its low byte. It also covers the 0x7F shortcut, a missing
//! sector size, and a null heap block. State 1 and the fallthrough cover a
//! CD slot that is done or still busy. State 2 parses and frees with
//! _shpAllocated of either sign. The stubs write what the real callees can
//! change: the object's dataAddr, _shpAllocated, _shpData and the slot state.
//! They return -1, 0 or any value.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").loadShp));

pub const classes = [_][:0]const u8{
    "state0-found",
    "state0-not-found",
    "state0-model-7f",
    "state1-slot-done",
    "state1-slot-busy",
    "state2",
    "state-default",
};

/// The CD slot the stubs hand out (a spec var: stubs must not allocate).
var slot: *v.vs_main_CdQueueSlot = undefined;
/// A heap block the allocator stub hands out (never dereferenced).
var heap: ?*anyopaque = null;

pub const stubs = struct {
    pub fn vs_main_allocHeapR(ctx: *zd.Ctx, size: anytype) ?*anyopaque {
        _ = size;
        if (ctx.value(u8) < 64) return null; // the C's NULL test
        return heap;
    }

    pub fn vs_main_allocateCdQueueSlot(ctx: *zd.Ctx, file: anytype) [*c]v.vs_main_CdQueueSlot {
        _ = file;
        // The C reads the slot's state right after the call.
        slot.state = if (ctx.value(u8) < 128) 4 else ctx.value(i16);
        return slot;
    }

    pub fn _parseShp(ctx: *zd.Ctx, arg0: anytype) c_int {
        const a: *v.vs_battle_objectData = @ptrCast(@alignCast(arg0.?));
        // What the parser may change that the C reads back.
        if (ctx.value(bool)) a.dataAddr = ctx.value(u32);
        if (ctx.value(bool)) v.g._shpAllocated().* = ctx.value(i32);
        if (ctx.value(bool)) v.g._shpData().* = if (ctx.value(bool)) heap else null;
        const r = ctx.value(u8);
        if (r < 80) return -1;
        if (r < 96) return 0;
        return ctx.value(c_int);
    }
};

fn findMatch(a: *v.vs_battle_objectData) bool {
    const tbl = v.g.D_800F4538();
    for (tbl.*, 0..) |e, i| {
        if (e) |p| {
            if (@as(i32, @intCast(i)) != @as(i32, a.index) and
                @as(i32, p.unk6E6) == @as(i32, a.modelId)) return true;
        }
    }
    return false;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.vs_battle_objectData);
    const objs = [2]*v.D_800F4538_t{ ctx.alloc(v.D_800F4538_t), ctx.alloc(v.D_800F4538_t) };
    slot = ctx.alloc(v.vs_main_CdQueueSlot);
    heap = @as(*anyopaque, @ptrCast(ctx.allocBytes(64, 16)));

    const tbl = v.g.D_800F4538();
    // modelId 0..255 (the table index), or the 0x7F shortcut.
    const mid: i16 = if (ctx.chance(6)) 0x7F else ctx.range(i16, 0, 255);
    const midIdx: usize = @intCast(mid);
    const m16: u16 = @intCast(mid);
    // objs[0]: an exact match, a match in the low byte only, 0xFFFF, 0, or the top bit flipped.
    objs[0].unk6E6 = ctx.edge(u16, &.{ m16, m16, m16 + 256, 0xFFFF, 0, m16 ^ 0x8000 });
    objs[1].unk6E6 = if (ctx.chance(3)) m16 else ctx.int(u16);
    actor.modelId = mid;

    if (ctx.chance(4)) {
        // Exactly one matching entry, at k in 0..16.
        for (tbl) |*e| e.* = null;
        const k = ctx.range(usize, 0, 16);
        tbl[k] = objs[0];
        objs[0].unk6E6 = m16;
        actor.index = if (ctx.chance(3)) @as(i8, @intCast(k)) else ctx.pick(i8, &.{ -1, 0, 5, 16, 17 });
    } else {
        for (tbl) |*e| e.* = if (ctx.chance(3)) null else objs[ctx.range(usize, 0, 1)];
        actor.index = ctx.pick(i8, &.{ -1, 0, 5, 16, 17 });
    }

    const sizes = v.g._shpFileSectorSizes();
    for (0..256) |i| sizes[i] = if (ctx.chance(6)) 0 else ctx.int(u8);
    sizes[midIdx] = if (ctx.chance(3)) 0 else ctx.range(u8, 1, 255);
    const lbas = v.g._shpLbaOffsets();
    for (0..256) |i| lbas[i] = ctx.int(u16);

    v.g._loadShpState().* = ctx.edge(u8, &.{ 0, 0, 0, 0, 1, 1, 2, 3, 0x80, 0xFF });
    v.g._shpAllocated().* = ctx.pick(i32, &.{ 0, 1, -1, std.math.minInt(i32), 0x4000_0000 });
    v.g._shpCdSlot().* = slot;
    v.g._shpData().* = heap;
    slot.state = ctx.pick(i16, &.{ 4, 4, 0, 1, -1 });
    return .{actor};
}

pub fn classify(args: Args) usize {
    const a = args[0].?;
    const st = v.g._loadShpState().*;
    if (st == 2) return 5;
    if (st == 1) return if (slot.state == 4) 3 else 4;
    if (st != 0) return 6;
    if (a.modelId == 0x7F) return 2;
    return if (findMatch(a)) 0 else 1;
}
