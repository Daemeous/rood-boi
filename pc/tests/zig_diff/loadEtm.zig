//! zig_diff spec: _loadEtm -> load_etm.loadEtm (30DB0.c).
//! Inputs: an objdata with modelId 0..111, index 0..3 and a random unk11; the
//! sector-size table with zero entries and sizes at 128..255 (the sign bit),
//! random LBA offsets; the state at 2 (the free prong), 1 (the CD-wait prong)
//! or 0/3/255 (the default prong); the CD slot's state at 4 or not; the
//! D_800F244F entry zero, 1 or 0x80. The heap, CD and function stubs return
//! null, 0 or nonzero and move the CD state; LoadImage's rect is traced into
//! the arena so its contents are compared.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").loadEtm));

pub const classes = [_][:0]const u8{
    "state-2",
    "state-1-cd-pending",
    "state-1-ready-replace",
    "state-1-ready-fresh",
    "default-no-sectors",
    "default-sized",
};

var trace_rect: *v.RECT = undefined;

pub const stubs = struct {
    pub fn vs_main_allocHeapR(ctx: *zd.Ctx, size: anytype) ?*anyopaque {
        _ = size;
        if (ctx.value(u3) == 0) return null;
        return @ptrCast(ctx.allocBytes(64, 16));
    }

    pub fn vs_main_freeHeapR(ctx: *zd.Ctx, p: anytype) void {
        _ = ctx;
        _ = p;
    }

    pub fn vs_main_allocateCdQueueSlot(ctx: *zd.Ctx, file: anytype) [*c]v.vs_main_CdQueueSlot {
        _ = file;
        const s = ctx.alloc(v.vs_main_CdQueueSlot);
        s.state = if (ctx.value(u1) == 0) 4 else ctx.value(i16);
        return @ptrCast(s);
    }

    pub fn vs_main_freeCdQueueSlot(ctx: *zd.Ctx, slot: anytype) void {
        _ = ctx;
        _ = slot;
    }

    pub fn vs_main_cdEnqueue(ctx: *zd.Ctx, slot: anytype, vram: anytype) void {
        _ = vram;
        const s: *v.vs_main_CdQueueSlot = @ptrCast(@alignCast(slot));
        switch (ctx.value(u2)) {
            0 => s.state = 4,
            1 => s.state = ctx.value(i16),
            else => {},
        }
    }

    pub fn func_8007DFF0(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype) c_int {
        _ = a;
        _ = b;
        _ = c;
        if (ctx.value(u2) == 0) return 0;
        return ctx.value(c_int) | 1;
    }

    pub fn func_8007E0A8(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype) c_int {
        _ = a;
        _ = b;
        _ = c;
        if (ctx.value(u2) == 0) return 0;
        return ctx.value(c_int) | 1;
    }

    pub fn LoadImage(ctx: *zd.Ctx, rect: anytype, p: anytype) c_int {
        _ = p;
        const r: *const v.RECT = @ptrCast(@alignCast(rect));
        trace_rect.* = r.*;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const sizes = v.g._etmFileSectorSizes();
    for (0..112) |i| sizes[i] = if (ctx.chance(6)) 0 else ctx.pick(u8, &.{ 1, 127, 128, 255 });
    const lbas = v.g._etmLbaOffsets();
    for (0..112) |i| lbas[i] = ctx.int(u16);
    // The per-object byte at D_800F244F[index]: the game's index is 0..2, the harness maps a few more.
    for (0..4) |k| v.g.D_800F244F()[k] = ctx.pick(u8, &.{ 0, 0x80, 1 });

    const obj = ctx.alloc(v.vs_battle_objectData);
    obj.modelId = ctx.range(i16, 0, 111);
    obj.index = ctx.range(i8, 0, 3);
    obj.unk11 = ctx.int(u8);

    const st = v.g._loadEtmState();
    st.* = switch (ctx.range(u8, 0, 9)) {
        0, 1 => 2,
        2, 3, 4 => 1,
        else => ctx.edge(u8, &.{ 0, 3, 255 }),
    };

    const slot = ctx.alloc(v.vs_main_CdQueueSlot);
    slot.state = if (ctx.chance(2)) 4 else ctx.edge(i16, &.{ 0, 3, 5, -1 });
    v.g._etmFileCdSlot().* = slot;

    const block: [*]u16 = @ptrCast(@alignCast(ctx.allocBytes(64, 16)));
    v.g._etmData().* = block;

    trace_rect = ctx.alloc(v.RECT);
    return .{obj};
}

pub fn classify(args: Args) usize {
    const obj = args[0].?;
    const st = v.g._loadEtmState().*;
    if (st == 2) return 0;
    if (st == 1) {
        const slot = v.g._etmFileCdSlot().*.?;
        if (slot.state != 4) return 1;
        return if (v.g.D_800F244F()[@intCast(obj.index)] != 0) 2 else 3;
    }
    const sizes = v.g._etmFileSectorSizes();
    return if (sizes[@as(usize, @intCast(obj.modelId))] == 0) 4 else 5;
}
