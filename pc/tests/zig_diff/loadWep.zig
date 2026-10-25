//! zig_diff spec: _loadWep -> load_wep.loadWep (30DB0.c).
//! Inputs: an objectData with modelId 0, 1..127 (with the 0x7F, 0x80 and
//! 0x81 edges) or above 0x80, and any i8 index (0..19, the 4 and 20 edges,
//! -1, -128, 127). The wepModels table is random: null entries, entries
//! that match the modelId, and single-match tables that make the scan
//! succeed at one chosen slot (0..19). The global state is one of the
//! `_loadShpState` cases 0, 1, 2, 3 or 255, with a CD slot that is
//! sometimes in state 4 and sometimes not. The sector offset for the
//! modelId is 0, 1 or 255. The heap allocator returns null about 37% of
//! the time, the CD slot is always valid, and _parseWep returns -1 about a
//! quarter of the time and writes _wepFileLoaded (0, 1 or any i32).
//! Negative modelIds are not generated: they would index memory before the
//! sector table.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").loadWep));

pub const classes = [_][:0]const u8{ "early-return", "cd-wait", "parse-only", "found-in-table", "fresh-load" };

var slot: *v.vs_main_CdQueueSlot = undefined;
var heap: *anyopaque = undefined;

pub const stubs = struct {
    pub fn vs_main_allocHeapR(ctx: *zd.Ctx, size: anytype) ?*anyopaque {
        _ = size;
        if (ctx.value(u8) < 96) return null;
        return heap;
    }

    pub fn vs_main_freeHeapR(ctx: *zd.Ctx, p: anytype) void {
        _ = ctx;
        _ = p;
    }

    pub fn vs_main_allocateCdQueueSlot(ctx: *zd.Ctx, file: anytype) [*c]c.vs_main_CdQueueSlot {
        _ = file;
        // The C dereferences the returned slot at once: always a valid one.
        slot.state = ctx.value(i16);
        return @ptrCast(slot);
    }

    pub fn vs_main_freeCdQueueSlot(ctx: *zd.Ctx, s: anytype) void {
        _ = ctx;
        _ = s;
    }

    pub fn vs_main_cdEnqueue(ctx: *zd.Ctx, s: anytype, vram: anytype) void {
        _ = vram;
        // The CD read completes: the C checks this state after the enqueue.
        const q: *v.vs_main_CdQueueSlot = @ptrCast(@alignCast(s));
        q.state = if (ctx.value(u8) < 128) 4 else ctx.value(i16);
    }

    pub fn _parseWep(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        // The parser sets the loaded flag the C reads back.
        const k = ctx.value(u2);
        v.g._wepFileLoaded().* = if (k == 0) 0 else if (k == 1) 1 else ctx.value(i32);
        if (ctx.value(u8) < 64) return -1;
        return ctx.value(i32);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.vs_battle_objectData);

    const modelId: i16 = if (ctx.chance(5))
        ctx.edge(i16, &.{ 0, 0x7f, 0x80, 0x81, 0x7fff })
    else
        ctx.range(i16, 1, 127);
    obj.modelId = modelId;
    obj.index = if (ctx.chance(2))
        ctx.edge(i8, &.{ 0, 3, 4, 5, 19, 20, -1, -128, 127 })
    else
        ctx.range(i8, 0, 19);

    if (modelId >= 1 and modelId < 0x80) {
        v.g._wepFileSectorOffsets()[@as(usize, @intCast(modelId))] = ctx.edge(u8, &.{ 0, 1, 255 });
    }

    v.g._loadShpState().* = ctx.edge(u8, &.{ 0, 0, 0, 1, 2, 2, 3, 255 });
    v.g._wepFileLoaded().* = ctx.pick(i32, &.{ 0, 1, -1 });

    slot = ctx.alloc(v.vs_main_CdQueueSlot);
    slot.state = ctx.pick(i16, &.{ 4, 3, 0 });
    v.g._wepCdSlot().* = slot;
    heap = @ptrCast(ctx.allocBytes(16, 16));

    const models = v.g.vs_battle_wepModels();
    const mid8: u8 = @truncate(@as(u16, @bitCast(modelId)));
    const single: ?usize = if (ctx.chance(2)) ctx.range(usize, 0, 19) else null;
    for (0..20) |i| {
        if (single == null and ctx.chance(3)) {
            models[i] = null;
            continue;
        }
        const m = ctx.alloc(v.vs_battle_wepModels_t);
        if (single) |k| {
            m.modelId = if (i == k) mid8 else mid8 +% 1;
        } else {
            m.modelId = if (ctx.chance(3)) mid8 else ctx.int(u8);
        }
        psx.setBits(m, "actorId", if (ctx.chance(3)) 0 else ctx.range(u4, 1, 15));
        models[i] = m;
    }

    return .{obj};
}

pub fn classify(args: Args) usize {
    const obj = args[0].?;
    if (obj.modelId == 0 or obj.modelId >= 0x80) return 0;
    const st = v.g._loadShpState().*;
    if (st == 1) return 1;
    if (st == 2) return 2;
    const models = v.g.vs_battle_wepModels();
    for (models, 0..) |m, i| {
        const w = m orelse continue;
        if (@as(i32, obj.index) == @as(i32, @intCast(i))) continue;
        if (@as(i32, w.modelId) != @as(i32, obj.modelId)) continue;
        if (psx.getBits(w, "actorId") == 0 and obj.index >= 4) continue;
        return 3;
    }
    return 4;
}
