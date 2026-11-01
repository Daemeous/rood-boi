//! zig_diff spec: _parseShp -> parse_shp.parseShp (30DB0.c).
//! Inputs: the pass D_800E8F29 (0 or 1; any other leaves the C's actor and
//! data uninitialised); the object (slot index 0, 1, 2, 16 or another below
//! 17, model 6 for slot 0's palette reload, 0x7F, or another; dataAddr a
//! slot below 17 or an SHP blob's address; actorId 0xFF or a slot; unk11
//! 0xFC or not). Only the actors a path reads are arena actors (the slot's
//! own, the copied one, slot 16 for the shared-texture search, the owner):
//! each with unk5BB 0..27 or 0xFF and a model (unk68) of up to 32 bones whose
//! armatures' unk8 cross 3..6. The SHP blob: header, the model block (its
//! offsets small: relocated below 4 GB), the SEQ, then either 4 (a texture
//! shared with an actor of that model) or the texture header: width 256 or
//! not, height giving n 0..5, CLUT count, CLUTs. The texture slots
//! D_800F46A8 are free or taken around the search's windows;
//! func_8007DFF0 (the fallback) gives 0 or not; func_8009AC24 finds a slot
//! or not. The second pass has a polygon list (FT3 36, 0x34 at byte 11).
//! LoadImage and _loadSeq copy their stack arguments into arena records;
//! func_8009D468 is called with one argument (2842C.h's prototype), its
//! second register not compared.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").parseShp));

pub const classes = [_][:0]const u8{ "palette-reload", "no-actor", "empty-model", "copy", "parse-shared", "parse", "fix-uvs" };

var rect_record: *v.RECT = undefined;
var seq_record: *v.vs_battle_objectData = undefined;
var found_slot: u32 = 0;
var fallback: c_int = 0;

pub const stubs = struct {
    pub fn vs_main_allocHeap(ctx: *zd.Ctx, size: anytype) *anyopaque {
        _ = size;
        return @ptrCast(ctx.allocBytes(1024, 16));
    }
    pub fn LoadImage(ctx: *zd.Ctx, rect: anytype, p: anytype) c_int {
        _ = ctx;
        _ = p;
        const r: *const v.RECT = @ptrCast(@alignCast(rect));
        rect_record.* = r.*;
        return 0;
    }
    pub fn _loadSeq(ctx: *zd.Ctx, q: anytype) c_int {
        const o: *const v.vs_battle_objectData = @ptrCast(@alignCast(q));
        seq_record.dataType = o.dataType;
        seq_record.index = o.index;
        seq_record.dataAddr = o.dataAddr;
        seq_record.modelId = o.modelId;
        seq_record.actorId = o.actorId;
        return ctx.value(c_int);
    }
    pub fn func_8009AC24(ctx: *zd.Ctx, o: anytype) c_int {
        _ = ctx;
        if (found_slot == 0) return 0;
        const obj: *v.vs_battle_objectData = @ptrCast(@alignCast(o.?));
        obj.dataAddr = found_slot;
        return 1;
    }
    pub fn func_8007DFF0(ctx: *zd.Ctx, a: anytype, b: anytype, d: anytype) c_int {
        _ = ctx;
        _ = a;
        _ = b;
        _ = d;
        return fallback;
    }
};

pub fn uninitialisedArgument(call: []const u8, index: usize) bool {
    return index == 1 and std.mem.eql(u8, call, "func_8009D468");
}

fn model(ctx: *zd.Ctx, bones: u8) *v.D_800F4538_unk68 {
    const m: *v.D_800F4538_unk68 = @ptrCast(@alignCast(ctx.allocBytes(64 + 32 * 16, 16)));
    const arm: [*]v._armature_t = @ptrCast(&m.armatures);
    for (0..bones) |i| arm[i].unk8 = ctx.edge(u8, &.{ 2, 3, 4, 6, 7, 255 });
    return m;
}

fn newActor(ctx: *zd.Ctx) *v.D_800F4538_t {
    const a: *v.D_800F4538_t = @ptrCast(@alignCast(ctx.allocBytes(0x1900, 16)));
    a.unk5BB = if (ctx.chance(4)) 0xFF else ctx.range(u8, 0, 23);
    a.unk0.nBones = ctx.range(u8, 0, 32);
    psx.put(&a.unk0.unk68, model(ctx, a.unk0.nBones));
    psx.setBits(&a.unk0, "unk8_4", ctx.range(u3, 0, 3));
    return a;
}

/// An SHP file as the first pass reads it.
fn blob(ctx: *zd.Ctx, actors: *[17]?*v.D_800F4538_t) u32 {
    const p = ctx.allocBytes(2048, 16);
    for (0..2048) |i| p[i] = ctx.int(u8);
    var at: usize = 4;
    // The header: nBones (<= 32), nVertexGroups, nTriangles, nQuads, nOther (<= 2 each: the second pass's walk).
    p[at] = ctx.range(u8, 0, 32);
    std.mem.writeInt(u16, p[at + 2 ..][0..2], ctx.range(u16, 0, 2), .little);
    std.mem.writeInt(u16, p[at + 4 ..][0..2], ctx.range(u16, 0, 2), .little);
    std.mem.writeInt(u16, p[at + 6 ..][0..2], ctx.range(u16, 0, 2), .little);
    at += 8 + 0xE8;
    const size: u32 = 64 + 32 * 16;
    std.mem.writeInt(u32, p[at..][0..4], size, .little);
    at += 4;
    // The model's offsets (48..64): small, relocated by the block's address; the polygons in the
    // allocHeap block (1024 bytes) past the copied model.
    for (0..3) |k| std.mem.writeInt(u32, p[at + 48 + 4 * k ..][0..4], ctx.range(u32, 0, 0xFFFF), .little);
    std.mem.writeInt(u32, p[at + 60 ..][0..4], size, .little);
    at += size;
    at += 4; // the SEQ
    const skip: u32 = ctx.range(u32, 0, 16);
    std.mem.writeInt(u32, p[at..][0..4], skip, .little);
    at += 4 + skip;
    const shared = ctx.chance(4);
    std.mem.writeInt(u32, p[at..][0..4], if (shared) 4 else ctx.edge(u32, &.{ 0, 5, 8 }), .little);
    at += 4;
    if (shared) {
        const id: u16 = ctx.edge(u16, &.{ 3, 9 });
        std.mem.writeInt(u32, p[at..][0..4], id, .little);
        // The search ends on slot 16 when nothing matches: it must exist.
        if (actors[16] == null) actors[16] = newActor(ctx);
        for (actors) |slot| if (slot) |a| {
            a.unk6E6 = if (ctx.chance(3)) id else 77;
        };
    } else {
        p[at] = ctx.int(u8); // the flags byte
        p[at + 1] = ctx.edge(u8, &.{ 128, 64, 32 }); // width / 2 (128: 0x100)
        p[at + 2] = ctx.edge(u8, &.{ 16, 32, 64, 96, 128, 160 }); // height / 2: n = 0..5
        p[at + 3] = ctx.range(u8, 0, 64); // the CLUT count
    }
    return psx.addr(p);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    rect_record = ctx.alloc(v.RECT);
    seq_record = ctx.alloc(v.vs_battle_objectData);
    const object = ctx.alloc(v.vs_battle_objectData);
    const actors = v.g.D_800F4538();
    for (actors) |*slot| slot.* = null;
    const textures = v.g.D_800F46A8();
    for (0..25) |i| textures[i].refCount = if (ctx.chance(2)) 0 else ctx.int(u8);
    fallback = if (ctx.chance(2)) 0 else 1;
    const effects = v.g.D_800F4528();
    for (0..3) |i| effects[i] = if (ctx.chance(2)) null else @ptrCast(ctx.allocBytes(16, 16));
    found_slot = 0;

    const pass = v.g.D_800E8F29();
    pass.* = if (ctx.chance(5)) 1 else 0;
    object.index = ctx.edge(i8, &.{ 0, 0, 1, 2, 16, 5 });
    object.modelId = if (object.index == 0 and ctx.chance(3)) 6 else ctx.edge(i16, &.{ 0x7F, 3, 9, 6 });
    object.variant = ctx.range(u8, 0, 2);
    object.actorId = if (ctx.chance(2)) 0xFF else ctx.range(u8, 0, 16);
    object.unk11 = if (ctx.chance(2)) 0xFC else ctx.int(u8);
    const own = &actors[@intCast(object.index)];
    if (pass.* == 0 and object.index != 0 and ctx.chance(5)) {
        // An empty slot: unk4 gives the actor, or nothing.
        psx.put(&object.unk4, if (ctx.chance(3)) null else @as(*anyopaque, @ptrCast(newActor(ctx))));
    } else {
        own.* = newActor(ctx);
    }
    if (object.actorId != 0xFF and object.unk11 == 0xFC and actors[object.actorId] == null) actors[object.actorId] = newActor(ctx);

    if (pass.* == 1) {
        const a = own.*.?;
        a.unk0.nOther = ctx.range(u16, 0, 2);
        a.unk0.nTriangles = ctx.range(u16, 0, 2);
        a.unk0.nQuads = ctx.range(u16, 0, 2);
        psx.setBits(&a.unk0, "unk8_3", ctx.int(u1));
        if (a.unk5BB == 0xFF) a.unk5BB = ctx.range(u8, 0, 23);
        const prims = ctx.allocBytes(6 * 24, 16);
        for (0..6 * 24) |i| prims[i] = if (ctx.chance(4)) ctx.edge(u8, &.{ 36, 0x34 }) else ctx.int(u8);
        psx.put(&psx.get(a.unk0.unk68).?.polyOffset, @as(*anyopaque, @ptrCast(prims)));
        object.dataAddr = ctx.int(u32);
    } else if (object.index == 0 and object.modelId == 6) {
        // Slot 0's palette reload: skip 0xF4, a block, 4, another block, 12, then the CLUTs.
        const p = ctx.allocBytes(1024, 16);
        std.mem.writeInt(u32, p[0xF4..][0..4], 8, .little);
        std.mem.writeInt(u32, p[0xF4 + 16 ..][0..4], 8, .little);
        own.*.?.unk0.unkE = ctx.range(u8, 0, 64);
        if (own.*.?.unk5BB == 0xFF) own.*.?.unk5BB = ctx.range(u8, 0, 23);
        object.dataAddr = psx.addr(p);
    } else if (ctx.chance(4)) {
        object.dataAddr = ctx.range(u32, 0, 16);
        if (actors[object.dataAddr] == null) actors[object.dataAddr] = newActor(ctx);
    } else {
        object.dataAddr = blob(ctx, actors);
        if (ctx.chance(4)) {
            found_slot = ctx.range(u32, 1, 16);
            if (actors[found_slot] == null) actors[found_slot] = newActor(ctx);
        }
    }
    // Free windows around 4, 12 and 20 for the texture searches.
    if (ctx.chance(2)) {
        const start = ctx.edge(usize, &.{ 0, 4, 8, 12, 16 });
        for (start..start + 4) |i| textures[i].refCount = 0;
    }
    return .{object};
}

pub fn classify(args: Args) usize {
    const object = args[0];
    if (v.g.D_800E8F29().* == 1) return 6;
    if (object.index == 0 and object.modelId == 6) return 0;
    const own = v.g.D_800F4538()[@intCast(object.index)];
    if (own == null and psx.get(object.unk4) == null) return 1;
    if (object.modelId == 0x7F) return 2;
    if (object.dataAddr < 17 or found_slot != 0) return 3;
    const p: [*]const u8 = @ptrFromInt(object.dataAddr);
    var at: usize = 4 + 8 + 0xE8 + 4 + 64 + 32 * 16 + 4;
    at += 4 + std.mem.readInt(u32, p[at..][0..4], .little);
    return if (std.mem.readInt(u32, p[at..][0..4], .little) == 4) 4 else 5;
}
