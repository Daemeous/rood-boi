//! zig_diff spec: _parseWep -> parse_wep.parseWep (30DB0.c).
//! Inputs: the pass D_800E8F29 (0 most often, 1, or another value); the
//! object's slot index 0..19 (the common buffers at 0 and 1, the < 4 copy
//! of the offsets); the model table vs_battle_wepModels with arena models
//! (texSlot 0..19 or 0xFF, offsets an arena block) or nulls, the object's
//! own slot null with its unk4 null or a fresh model; dataAddr a slot number
//! below 20 or the address of a WEP blob built in the arena (header, an
//! offsets block of 16..200 bytes, the texture width 32 or 64 pixels wide
//! or wider, the CLUT size 96 or 48, the palettes), and other models whose
//! modelId and actorId make the search find one or not. The texture slots
//! vs_battle_wepTextures are free or taken (all taken 1 time in 6). The
//! second pass has a polygon list of FT3 (36) and other prims, an epilog
//! with the bone info. LoadImage copies its RECT (a stack local the trace
//! only tags) into an arena record so the rectangle is compared.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").parseWep));

pub const classes = [_][:0]const u8{ "no-model", "busy", "copy-slot", "copy-found", "parse-narrow", "parse-wide", "parse-full", "fix-prims", "other-pass" };

var rect_record: *v.RECT = undefined;

pub const stubs = struct {
    pub fn vs_main_allocHeap(ctx: *zd.Ctx, size: anytype) *anyopaque {
        _ = size;
        return @ptrCast(ctx.allocBytes(2048, 16));
    }
    pub fn LoadImage(ctx: *zd.Ctx, rect: anytype, p: anytype) c_int {
        _ = ctx;
        _ = p;
        const r: *const v.RECT = @ptrCast(@alignCast(rect));
        rect_record.* = r.*;
        return 0;
    }
    pub fn func_800A152C(ctx: *zd.Ctx, a: anytype, b: anytype, m: anytype) c_int {
        _ = a;
        _ = b;
        _ = m;
        return ctx.value(c_int);
    }
};

fn newModel(ctx: *zd.Ctx) *v.vs_battle_wepModels_t {
    const m = ctx.alloc(v.vs_battle_wepModels_t);
    m.texSlot = if (ctx.chance(3)) 0xFF else ctx.range(u8, 0, 19);
    m.nBones = if (ctx.chance(3)) 0 else ctx.range(u8, 1, 8);
    psx.put(&m.offsets, @as(*v._wepModelOffsets, @ptrCast(@alignCast(ctx.allocBytes(2048, 16)))));
    return m;
}

/// A WEP file: what the first pass reads (the texture itself is LoadImage's).
fn blob(ctx: *zd.Ctx) u32 {
    const size: u32 = ctx.edge(u32, &.{ 16, 64, 200 });
    const total: usize = 16 + size + 8 + 64 + 7 * 128 + 16;
    const p = ctx.allocBytes(total, 16);
    for (0..total) |i| p[i] = ctx.int(u8);
    std.mem.writeInt(u32, p[12..16], size, .little);
    const tail = p + 16 + size;
    tail[5] = ctx.pick(u8, &.{ 16, 32, 33, 64, 128 }); // half the width: texW = 2 * this, < 65 or not
    tail[7] = if (ctx.chance(2)) 96 else ctx.pick(u8, &.{ 48, 95, 97 });
    return psx.addr(p);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    rect_record = ctx.alloc(v.RECT);
    const object = ctx.alloc(v.vs_battle_objectData);
    const index = ctx.edge(i8, &.{ 0, 1, 2, 3, 4, 7, 19 });
    object.index = index;
    object.modelId = ctx.edge(i16, &.{ 5, 6, 7, 300 });
    object.actorId = ctx.int(u8);

    const models = v.g.vs_battle_wepModels();
    for (models) |*slot| {
        slot.* = if (ctx.chance(3)) null else newModel(ctx);
        if (slot.*) |m| {
            m.modelId = ctx.edge(u8, &.{ 5, 6, 7, 44 });
            psx.setBits(m, "actorId", if (ctx.chance(2)) 0 else ctx.int(u4));
        }
    }
    const own = &models[@intCast(index)];
    const pass = v.g.D_800E8F29();
    pass.* = ctx.edge(u8, &.{ 0, 0, 0, 0, 1, 1, 2 });
    if (pass.* == 0 and ctx.chance(3)) {
        own.* = null;
        psx.put(&object.unk4, if (ctx.chance(4)) null else @as(*anyopaque, @ptrCast(ctx.alloc(v.vs_battle_wepModels_t))));
    } else if (own.* == null) {
        own.* = newModel(ctx);
    }
    if (pass.* != 0 and own.* != null) own.*.?.nBones = ctx.range(u8, 0, 12);
    object.dataAddr = if (pass.* == 0 and ctx.chance(4)) ctx.range(u32, 0, 19) else blob(ctx);
    if (object.dataAddr < 20 and models[object.dataAddr] == null) models[object.dataAddr] = newModel(ctx);

    const textures = v.g.vs_battle_wepTextures();
    const full = ctx.chance(6);
    for (textures) |*t| t.refCount = if (!full and ctx.chance(3)) 0 else ctx.range(u8, 1, 255);
    const buffers = v.g.vs_battle_commonWepBuffs();
    buffers[0] = @ptrCast(@alignCast(ctx.allocBytes(2048, 16)));
    buffers[1] = @ptrCast(@alignCast(ctx.allocBytes(2048, 16)));

    // The second pass: the polygons and the epilog (the bone info).
    if (pass.* == 1) {
        const m = own.*.?;
        m.nOther = ctx.range(u16, 0, 2);
        m.nTriangles = ctx.range(u16, 0, 2);
        m.nQuads = ctx.range(u16, 0, 2);
        const prims = ctx.allocBytes(6 * 20, 16);
        for (0..6 * 20) |i| prims[i] = if (ctx.chance(3)) 36 else ctx.int(u8);
        psx.get(m.offsets).?.polygonsOffset = @bitCast(psx.addr(prims));
    }
    v.g._wepFileEpilog().* = @ptrCast(ctx.allocBytes(128, 16));
    return .{object};
}

pub fn classify(args: Args) usize {
    const object = args[0];
    const pass = v.g.D_800E8F29().*;
    const models = v.g.vs_battle_wepModels();
    const own = models[@intCast(object.index)];
    if (pass == 1) return 7;
    if (pass != 0) return 8;
    if (own == null and psx.get(object.unk4) == null) return 0;
    if (own) |m| if (m.nBones != 0) return 1;
    if (object.dataAddr < 20) return 2;
    for (models, 0..) |slot, j| {
        if (j == @as(usize, @intCast(object.index))) continue;
        if (slot) |other| {
            if ((psx.getBits(other, "actorId") != 0 or object.index < 4) and @as(i32, other.modelId) == object.modelId) return 3;
        }
    }
    const textures = v.g.vs_battle_wepTextures();
    var free: usize = 0;
    for (textures) |t| free += @intFromBool(t.refCount == 0);
    if (free == 0) return 6;
    const size = std.mem.readInt(u32, @as(*const [4]u8, @ptrFromInt(object.dataAddr + 12)), .little);
    const half = @as(*const u8, @ptrFromInt(object.dataAddr + 16 + size + 5)).*;
    return if (@as(u32, half) * 2 < 65) 4 else 5;
}
