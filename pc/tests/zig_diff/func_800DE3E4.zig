//! zig_diff spec: func_800DE3E4 -> func_800DE3E4.func_800DE3E4 (74088.c).
//! Inputs: a plan state (id 0 or not, minDelay and the stats' unk954/35 at,
//! below and above each other and the 455 delay, allowFallback both ways)
//! whose stats carry 24 armor ids below 256; a chain of 1-3 actors (ids
//! 0-15) for the D_800F5904 used-table clear and the stat snapshot; the heap
//! (vs_main_allocHeapR) full sometimes. The scoring stub (func_800DD918)
//! hands back 0-16 candidates from a pool whose thresholds sit 79-81 apart
//! (the spread test at its edge) and sometimes a fallback list; after a
//! failed snapshot it yields at most one (the C then reads its uninitialised
//! quotas only when two or more remain). func_800DC2E0 copies the entry.
//! D_800F5920 null or not, with the boss check's flag 4, unk9 24, unkC 4
//! and subTypes 171-174.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_74088");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800DE3E4));

pub const classes = [_][:0]const u8{ "no-id", "planned", "nothing-scored" };

var pool: *[16]v.actionCandidateEntry = undefined;
var heap: [*]u8 = undefined;
var failed = false;
/// No candidate in any pass (the fallback list is then used).
var empty = false;

pub const stubs = struct {
    pub fn vs_main_allocHeapR(ctx: *zd.Ctx, size: anytype) ?*anyopaque {
        _ = size;
        failed = ctx.value(u8) < 48;
        return if (failed) null else @ptrCast(heap);
    }
    pub fn func_800DD918(ctx: *zd.Ctx, state: anytype, threshold: anytype) void {
        _ = threshold;
        const s: *v.actionPlanState = @ptrCast(@alignCast(state));
        const n: c_int = if (failed) ctx.value(u1) else if (empty) 0 else if (ctx.value(u8) < 96) 16 else ctx.value(u4);
        s.count = n;
        var i: usize = 0;
        while (i < @as(usize, @intCast(n))) : (i += 1) psx.put(&s.entries[i], &pool[(i + ctx.value(u4)) % 16]);
        if (!failed and ctx.value(u8) < 128) psx.global(c_int, "D_800F58F0").* = ctx.value(u4);
    }
    pub fn func_800DC2E0(ctx: *zd.Ctx, dst: anytype, src: anytype) void {
        _ = ctx;
        const d: *v.actionCandidateEntry = @ptrCast(@alignCast(dst));
        const s: *const v.actionCandidateEntry = @ptrCast(@alignCast(src));
        d.* = s.*;
    }
    pub fn rand(ctx: *zd.Ctx) c_int {
        return if (ctx.value(u1) == 1) 0 else ctx.value(c_int);
    }
    pub fn vs_battle_getStateFlag(ctx: *zd.Ctx, id: anytype) u8 {
        _ = id;
        return if (ctx.value(u1) == 1) 4 else ctx.value(u8);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const s = ctx.alloc(v.actionPlanState);
    const stats = ctx.alloc(v.vs_battle_actor2);
    psx.put(&s.stats, stats);
    for (&stats.armor) |*r| {
        for (r) |*a| a.id = ctx.range(i16, 0, 255);
    }
    s.id = if (ctx.chance(8)) 0 else ctx.int(u8) | 1;
    empty = ctx.chance(4);
    s.allowFallback = if (ctx.chance(2)) 0 else 1;
    const base: u16 = ctx.edge(u16, &.{ 0, 1, 7, 13, 255 });
    stats.unk954 = switch (ctx.range(u8, 0, 4)) {
        0 => base * 35,
        1 => base * 35 + 34,
        2 => 455 * 35,
        3 => (base + 1) * 35,
        else => ctx.int(u16),
    };
    s.minDelay = @intCast(base);
    if (ctx.chance(4)) s.minDelay = ctx.int(u8);

    pool = ctx.alloc([16]v.actionCandidateEntry);
    const t: u16 = ctx.int(u16);
    for (pool) |*e| {
        ctx.random.bytes(std.mem.asBytes(e));
        psx.setBits(e, "threshold", t +% ctx.edge(u16, &.{ 0, 79, 80, 81, 160, 0xFFB0 }));
    }
    heap = ctx.allocBytes(64, 16);
    psx.global(?[*][16]v.actionCandidateEntry, "D_800F58EC").* = @ptrCast(ctx.alloc([3][16]v.actionCandidateEntry));
    const fb = ctx.alloc([16]v.actionCandidateEntry);
    for (fb) |*e| ctx.random.bytes(std.mem.asBytes(e));
    psx.global(?[*]v.actionCandidateEntry, "D_800F58F8").* = fb;
    psx.global(?[*][256]u8, "D_800F5904").* = @ptrCast(ctx.alloc([16][256]u8));
    v.g.D_800F58BC().* = ctx.alloc(v.D_800F58BC_t);

    const count = ctx.range(usize, 1, 3);
    var first: ?*v.vs_battle_actor = null;
    var k: usize = 0;
    while (k < count) : (k += 1) {
        const actor = ctx.alloc(v.vs_battle_actor);
        psx.put(&actor.next, first);
        psx.put(&actor.stats, ctx.alloc(v.vs_battle_actor2));
        actor.id = ctx.range(c_int, 0, 15);
        first = actor;
    }
    v.g.vs_battle_actors()[0] = first;
    const self = ctx.alloc(v.vs_battle_actor);
    psx.put(&s.actor, self);
    self.unk9 = if (ctx.chance(2)) 24 else ctx.int(u8);
    self.unkC = if (ctx.chance(2)) 4 else ctx.int(i32);
    self.subType = ctx.range(u16, 171, 174);
    if (ctx.chance(4)) {
        v.g.D_800F5920().* = null;
    } else {
        const f = ctx.alloc(v.D_800F5920_t);
        f.unk16 = if (ctx.chance(2)) 0 else 1;
        v.g.D_800F5920().* = f;
    }
    return .{s};
}

pub fn classify(args: Args) usize {
    if (args[0].?.id == 0) return 0;
    return if (empty) 2 else 1;
}
