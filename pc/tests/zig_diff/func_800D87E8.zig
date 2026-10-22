//! zig_diff spec: func_800D87E8 -> func_800D87E8.func_800D87E8 (6FC00.c).
//! Inputs: a random actorRelationState (its own object, phase, unk22/unk25
//! flags often zero, distances and the far/near/angle limits drawn from small
//! edge sets so ordered tests see equal sides, relation counter 120 and kind 3
//! often so the counter reaches 121) and an id 0..15. The tables the C reads are
//! built in the arena and the scratchpad: 17 actor objects in D_800F4538 (shared
//! by the actor list's unk44), 18 target states (sometimes null), 16 list nodes
//! (length random; the head is null for ids other than 0 sometimes), 16
//! scratchpad relation pointers (null half the time), and the tile table's
//! row index kept at 31 or below, and the movement grid the scratchpad
//! slot 0x1F8003C0 points at (rows 0..31, columns up to 255). The geometry callees are stubs that return
//! small values so distance and radius comparisons hit equal sides; checkExtent
//! and checkHeight are the unit's own statics and run for real.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_6FC00");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D87E8));

pub const classes = [_][:0]const u8{ "no-target", "self-actor", "dlist-skip", "linked", "plain" };

pub const stubs = struct {
    /// The extent/height test checkExtent and checkHeight return: 0 (the `!`
    /// edge), 1, -1 or any value.
    pub fn func_800E44E8(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype, d: anytype) c_int {
        _ = a;
        _ = b;
        _ = c;
        _ = d;
        return switch (ctx.value(u2)) {
            0 => 0,
            1 => 1,
            2 => -1,
            else => ctx.value(c_int),
        };
    }
    pub fn func_800E4624(ctx: *zd.Ctx, a: anytype, b: anytype, h1: anytype, h2: anytype) c_int {
        _ = a;
        _ = b;
        _ = h1;
        _ = h2;
        if (ctx.value(u4) == 0) return -1;
        return @as(c_int, ctx.value(u3)) * 64;
    }
    pub fn func_800E437C(ctx: *zd.Ctx, a: anytype, b: anytype, h: anytype, d: anytype) c_int {
        _ = a;
        _ = b;
        _ = h;
        _ = d;
        return @as(c_int, ctx.value(u3)) * 32;
    }
    pub fn ratan2(ctx: *zd.Ctx, y: anytype, x: anytype) c_int {
        _ = y;
        _ = x;
        return @as(c_int, ctx.value(u12)) - 2048;
    }
    pub fn rcos(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        if (ctx.value(u8) < 64) return 0;
        return @as(c_int, ctx.value(i16));
    }
    pub fn rsin(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        return @as(c_int, ctx.value(u4)) - 8;
    }
    pub fn vs_gte_rsqrt(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        return @as(c_int, ctx.value(u4)) << 12;
    }
};

fn newState(ctx: *zd.Ctx) *v.actorRelationState {
    const t = ctx.alloc(v.actorRelationState);
    t.radius = ctx.pick(u16, &.{ 0, 1, 8, 63, 64 });
    return t;
}

fn setScratchRelation(k: usize, val: u32) void {
    @as(*align(1) u32, @ptrFromInt(zd.scratchpad + 0x37C + 4 * k)).* = val;
}

fn configObject(ctx: *zd.Ctx, o: *v.actorObject) void {
    o.flags.raw = ctx.pick(u32, &.{ 0, 0x80000, 0x100000, 0x180000 });
    o.tile._bytes[0] = ctx.int(u8);
    o.tile._bytes[2] = ctx.range(u8, 0, 31);
    o.lastTile._bytes[0] = ctx.int(u8);
    o.lastTile._bytes[2] = ctx.range(u8, 0, 31);
    o.position.vy = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const uid: usize = ctx.range(usize, 0, 15);
    const id: c_int = @intCast(uid);

    const st = ctx.alloc(v.actorRelationState);
    const own = ctx.alloc(v.actorObject);
    configObject(ctx, own);
    st.object.raw = psx.addr(own);

    st.actorId = ctx.range(u8, 0, 15);
    if (ctx.chance(4)) st.actorId = @intCast(uid);
    if (ctx.chance(3)) st.unk22 = 0;
    if (ctx.chance(3)) st.unk25 = 0;
    st.position.vy = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    st.farDistance = ctx.pick(u32, &.{ 0, 64, 128, 256, 384, 448, 0xFFFF_FFFF, 0x8000_0000 });
    st.nearDistance = ctx.pick(u32, &.{ 0, 64, 128, 256, 384, 448 });
    st.angleLimit = ctx.pick(u32, &.{ 0, 32, 64, 96, 128, 0x7FFF_FFFF, 0x8000_0000, 0xFFFF_FFFF });
    for (&st.distances) |*d| {
        d.* = if (ctx.chance(8)) 0xFFFF_FFFF else ctx.range(u32, 0, 7) * 64;
    }

    // Relation bits of this id: counter near the 121 edge, kind 3, kind timer 0 or 1.
    const rb = &st.relations[uid].bits;
    if (ctx.chance(3)) psx.setBits(rb, "counter", if (ctx.chance(2)) 119 else 120);
    if (ctx.chance(2)) psx.setBits(rb, "kind", 3);
    if (ctx.chance(2)) {
        psx.setBits(rb, "kindTimer", 0);
    } else if (ctx.chance(2)) {
        psx.setBits(rb, "kindTimer", 1);
    }

    // The 17 D_800F4538 objects (each also read as actorObject).
    const d4538 = v.g.D_800F4538();
    for (d4538) |*slot| {
        const o = ctx.alloc(v.actorObject);
        configObject(ctx, o);
        slot.* = @ptrCast(@alignCast(o));
    }
    const tgt: *v.actorObject = @ptrCast(@alignCast(d4538[uid].?));
    if (ctx.chance(3)) tgt.position.vy = st.position.vy;
    if (ctx.chance(3)) {
        st.tile._bytes[0] = tgt.tile._bytes[0];
        st.tile._bytes[2] = tgt.tile._bytes[2];
    }
    if (ctx.chance(4)) {
        tgt.lastTile._bytes[0] = st.tile._bytes[0];
        tgt.lastTile._bytes[2] = st.tile._bytes[2];
    }

    // D_800F45E0: the first test of the skip condition; non-null for this id a third of the time.
    const d45 = v.g.D_800F45E0();
    for (d45) |*slot| slot.* = null;
    if (ctx.chance(3)) d45[uid] = @ptrCast(@alignCast(tgt));

    // D_800F5878: 18 target states, the id's sometimes null.
    const d5878 = v.g.D_800F5878();
    var k: usize = 0;
    while (k < 18) : (k += 1) {
        d5878[k] = null;
        if (!ctx.chance(6)) d5878[k] = @ptrCast(@alignCast(newState(ctx)));
    }

    // Scratchpad RELATION_STATES(k): linked half the time.
    k = 0;
    while (k < 16) : (k += 1) {
        var link: u32 = 0;
        if (!ctx.chance(2)) link = psx.addr(newState(ctx));
        setScratchRelation(k, link);
    }

    // vs_battle_actors: 16 actors, id = index, unk44 = that index's object, list of random length.
    const tab = v.g.vs_battle_actors();
    var acts: [16]*v.vs_battle_actor = undefined;
    for (0..16) |j| {
        const ac = ctx.alloc(v.vs_battle_actor);
        ac.id = @intCast(j);
        ac.unk44.raw = psx.addr(d4538[j].?);
        acts[j] = ac;
    }
    const len = ctx.range(usize, 1, 16);
    for (0..16) |j| {
        acts[j].next.raw = if (j + 1 < len) psx.addr(acts[j + 1]) else 0;
    }
    for (0..16) |j| tab[j] = acts[j];
    if (uid != 0 and ctx.chance(6)) tab[0] = null;

    // The movement grid (u_short (*)[32]) the scratchpad slot 0x1F8003C0 points
    // at: rows 0..31 and columns up to 255 (x is any byte), random cells.
    const grid = ctx.allocBytes((31 * 32 + 256) * 2, 4);
    @as(*align(1) u32, @ptrFromInt(zd.scratchpad + 0x3C0)).* = psx.addr(@as(*u16, @ptrCast(@alignCast(grid))));

    return .{ st, id };
}

pub fn classify(args: Args) usize {
    const st = args[0].?;
    const uid: usize = @intCast(args[1]);
    if (v.g.D_800F5878()[uid] == null) return 0;
    if (st.actorId == uid) return 1;
    if (v.g.D_800F45E0()[uid] != null) return 2;
    const link = @as(*align(1) const u32, @ptrFromInt(zd.scratchpad + 0x37C + 4 * uid)).*;
    return if (link != 0) 3 else 4;
}
