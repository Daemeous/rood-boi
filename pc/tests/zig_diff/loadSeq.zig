//! zig_diff spec: _loadSeq -> load_seq.loadSeq (30DB0.c).
//! Inputs: an object with every actorId branch (0 with modelId 0, 255, other
//! and the shared-sequence search; 1, 2, 3, 4, 5 and the other ids), the
//! state byte at 0, 1 and 2, data pointers into an arena buffer or small
//! actor indices (the "< 17" copy path), counts from -1 to 15 with the seq
//! blocks they point at, size words around the alignment and the nBones
//! test, null and non-null actor slots, and the allocator, memcpy and
//! loadEtm results the C tests (NULL, -1, 0).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").loadSeq));

pub const classes = [_][:0]const u8{ "etm", "actor0-modelId0", "actor0-modelId255", "actor0-other", "actor3", "actor4-5", "else" };

fn addr32(p: anytype) u32 {
    return @truncate(@intFromPtr(p));
}

fn put32(p: [*]u8, off: usize, x: u32) void {
    const q: *align(1) u32 = @ptrCast(p + off);
    q.* = x;
}

fn put16(p: [*]u8, off: usize, x: u16) void {
    const q: *align(1) u16 = @ptrCast(p + off);
    q.* = x;
}

fn block(ctx: *zd.Ctx) [*]u8 {
    const b = ctx.allocBytes(16, 16);
    @memset(b[0..16], 0);
    return b;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const object = ctx.alloc(v.vs_battle_objectData);
    const data: [*]u8 = ctx.allocBytes(512, 16);

    const ids = [_]u8{ 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 255 };
    const id = ctx.edge(u8, &ids);
    const idx_all = id == 0 or id == 3 or id == 4 or id == 5;
    const index: i8 = if (idx_all) ctx.range(i8, 0, 16) else ctx.range(i8, 1, 4);
    const idx: usize = @intCast(index);
    const modelId: i16 = ctx.edge(i16, &.{ 0, 0, 1, 2, -1, 255, 0x7fff });

    object.actorId = id;
    object.index = index;
    object.modelId = modelId;
    v.g.D_800E8F2B().* = ctx.edge(u8, &.{ 0, 0, 0, 1, 2 });

    const small = id == 0 and modelId != 255 and ctx.chance(3);
    object.dataAddr = if (small) ctx.range(u32, 0, 16) else addr32(data);
    v.g.D_800F22E4().* = data;

    // Actor pool: slots alias these, so the C sees matching and differing actors.
    var pool: [4]*v.D_800F4538_t = undefined;
    for (&pool) |*a| {
        const p = ctx.alloc(v.D_800F4538_t);
        p.unk6E6 = ctx.edge(u16, &.{ 0, 1 });
        p.unk5B1 = if (modelId == 255) ctx.edge(u8, &.{ 0, 1, 2 }) else ctx.edge(u8, &.{ 0, 1, 2, 255 });
        p.unk0.nBones = ctx.edge(u8, &.{ 0, 1, 2, 3 });
        p.unk0.unkF = ctx.edge(u8, &.{ 0, 1 });
        p.unk5D8.raw = if (ctx.chance(3)) 0 else addr32(block(ctx));
        a.* = p;
    }

    var idx_actor: *v.D_800F4538_t = pool[0];
    for (0..17) |i| {
        const need = (idx_all and i == idx) or (small and i == @as(usize, object.dataAddr));
        const p: ?*v.D_800F4538_t = if (!need and ctx.chance(3)) null else pool[ctx.range(usize, 0, 3)];
        v.g.D_800F4538().*[i] = p;
        if (p) |q| if (i == idx) {
            idx_actor = q;
        };
    }

    v.g.D_800F4768().* = @ptrCast(block(ctx));
    v.g.D_800F4930().* = @ptrCast(block(ctx));
    const dk = v.g.D_800F4528();
    for (0..4) |s| {
        dk[s] = if (ctx.chance(3)) null else @ptrCast(block(ctx));
    }

    // Generic layout: byte at 0, u16 size at 2 (the nBones test), int size at 4.
    const sizes = [_]i32{ 0, 1, 2, 3, 4, 5, 8, 13, -1, -3, 0x7fff_fff0 };
    put32(data, 0, ctx.int(u32));
    const nb: u16 = idx_actor.unk0.nBones;
    put16(data, 2, ctx.edge(u16, &.{ 0, 1, nb, nb +% 1 }));
    put32(data, 4, @bitCast(ctx.edge(i32, &sizes)));

    // Else layout: count, the u16 pairs, then the raw seq offsets.
    if (id != 0 and id != 3 and id != 4 and id != 5) {
        const count = ctx.edge(i32, &.{ -1, 0, 1, 2, 3, 15 });
        put32(data, 0, @bitCast(count));
        const n: usize = if (count > 0) @intCast(count) else 0;
        const e_addr: u32 = addr32(data) +% @as(u32, @intCast(4 + 12 * n));
        for (0..n) |i| {
            put32(data, 4 + 4 * i, ctx.int(u16));
            put32(data, 4 + 4 * n + 4 * i, ctx.int(u16));
            const b = block(ctx);
            put32(data, 4 + 8 * n + 4 * i, addr32(b) -% 8 -% e_addr);
        }
    }

    return .{object};
}

pub fn classify(args: Args) usize {
    const object = args[0].?;
    if (v.g.D_800E8F2B().* == 1) return 0;
    return switch (object.actorId) {
        0 => if (object.modelId == 0) 1 else if (object.modelId == 255) 2 else 3,
        3 => 4,
        4, 5 => 5,
        else => 6,
    };
}

fn pointerOrNull(ctx: *zd.Ctx) u32 {
    if (ctx.value(u1) == 1) return 0;
    return @truncate(@intFromPtr(&zd.zdiff_arena[16]));
}

pub const stubs = struct {
    pub fn vs_main_allocHeap(ctx: *zd.Ctx, size: anytype) ?*anyopaque {
        _ = size;
        if (ctx.value(u8) < 40) return null;
        return @ptrCast(ctx.allocBytes(16, 16));
    }

    pub fn vs_main_memcpy(ctx: *zd.Ctx, dest: anytype, src: anytype, sz: anytype) void {
        _ = src;
        _ = sz;
        // The copy rewrites the seq's first two pointer fields, which the C reads back.
        const p: ?*anyopaque = dest;
        const q = p orelse return;
        const d: *[2]u32 = @ptrCast(@alignCast(q));
        d[0] = pointerOrNull(ctx);
        d[1] = pointerOrNull(ctx);
    }

    pub fn _loadEtm(ctx: *zd.Ctx, object: anytype) c_int {
        _ = object;
        return if (ctx.value(u8) < 64) -1 else 0;
    }

    pub fn func_8009C378(ctx: *zd.Ctx, seq: anytype, src: anytype) void {
        _ = src;
        const p: ?*anyopaque = seq;
        const q = p orelse return;
        const d: *[2]u32 = @ptrCast(@alignCast(q));
        d[0] = pointerOrNull(ctx);
        d[1] = pointerOrNull(ctx);
    }

    pub fn func_8009CAEC(ctx: *zd.Ctx, actor: anytype, slot: anytype) void {
        _ = slot;
        const p: ?*anyopaque = actor;
        const a: *v.D_800F4538_t = @ptrCast(@alignCast(p.?));
        a.unk5BA = ctx.value(u8);
    }
};
