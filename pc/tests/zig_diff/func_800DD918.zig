//! zig_diff spec: func_800DD918 -> func_800DD918.func_800DD918 (74088.c).
//! Inputs: a battle state whose 6x4 slots are randomly enabled, with modes forced
//! per class (0 = no case, 1, 2, 3), armor ids 0 or 1..255, and 0-3 actors in a
//! chain (or none) whose ids select 18 action-state objects. Those objects are
//! dead or alive (dead bytes 0x80, 1, 0xFF, 0), with headings at the edges. The
//! D_800F5904 table is random with many zero bytes, and actors' limbs are zeroed
//! at random so the maxHp tests go both ways. Stubs return small score values
//! (equal scores are common), a small rand() sometimes. The candidate copy stub
//! scrambles the copied threshold and the scoring stub reads it back, so the
//! threshold store the C makes into list[1] is observable.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_74088");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800DD918));

pub const classes = [_][:0]const u8{ "no-actors", "all-dead", "mode-0", "mode-1", "mode-2", "mode-3" };

var kind: usize = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    kind = ctx.range(usize, 0, 5);

    const state = ctx.alloc(v.actionChoiceState);
    const stats = ctx.alloc(v.vs_battle_actor2);
    psx.put(&state.stats, stats);
    for (0..6) |row| {
        for (0..4) |col| {
            stats.armor[row][col].id = if (ctx.chance(5)) 0 else ctx.range(i16, 1, 255);
            const cfg = &state.configs[row][col];
            const en: u1 = if (ctx.chance(5)) 0 else 1;
            psx.setBits(cfg, "enabled", en);
            const forced: u2 = if (kind >= 2 and !ctx.chance(8)) @intCast(kind - 2) else ctx.int(u2);
            psx.setBits(cfg, "mode", forced);
        }
    }

    // D_800F5878: 18 action-state objects the C reads dead and heading from.
    const table = v.g.D_800F5878();
    for (0..18) |i| {
        const o = ctx.alloc(v.actionChoiceState);
        o.dead = if (kind == 1) @as(u8, 1) else if (ctx.chance(3)) ctx.pick(u8, &.{ 0x80, 1, 0xFF }) else 0;
        o.heading = ctx.pick(i16, &.{ 0, -1, 1, -32768, 32767 });
        const fp: *v.func_800DEEA4_t2 = @ptrCast(@alignCast(o));
        table[i] = fp;
    }

    // D_800F5904: 18 rows of 256 bytes, many zero.
    const tab = ctx.allocBytes(18 * 256, 16);
    for (0..18 * 256) |i| {
        tab[i] = if (ctx.chance(2)) 0 else ctx.int(u8);
    }
    const t2: [*][256]u8 = @ptrCast(@alignCast(tab));
    v.g.D_800F5904().* = t2;

    // The actor chain from vs_battle_actors[0].
    const n: usize = if (kind == 0) 0 else ctx.range(usize, 1, 3);
    var nodes: [3]*v.vs_battle_actor = undefined;
    for (0..n) |i| {
        const a = ctx.alloc(v.vs_battle_actor);
        a.id = ctx.range(i32, 0, 17);
        const st = ctx.alloc(v.vs_battle_actor2);
        psx.put(&a.stats, st);
        for (0..6) |limb| {
            if (ctx.chance(2)) @memset(std.mem.asBytes(&st.limbs[limb]), 0);
        }
        nodes[i] = a;
    }
    for (0..n) |i| {
        psx.put(&nodes[i].next, if (i + 1 < n) nodes[i + 1] else null);
    }
    const heads = v.g.vs_battle_actors();
    heads[0] = if (n == 0) null else nodes[0];

    const threshold = ctx.pick(c_int, &.{ 0, -1, 1, 65535, 65536, std.math.maxInt(c_int) });
    return .{ state, threshold };
}

pub fn classify(args: Args) usize {
    _ = args;
    return kind;
}

pub const stubs = struct {
    pub fn func_800E4288(ctx: *zd.Ctx, buf: anytype, size: anytype) void {
        const p: [*]u8 = @ptrCast(@as(?*anyopaque, buf).?);
        for (0..@as(usize, @intCast(size))) |i| p[i] = ctx.value(u8);
    }

    pub fn func_800DCC94(ctx: *zd.Ctx, state: anytype, candidate: anytype, mode: anytype, action: anytype) void {
        _ = state;
        _ = mode;
        _ = action;
        const cand: *v.actionCandidateEntry = @ptrCast(@alignCast(@as(?*anyopaque, candidate).?));
        // The score depends on the candidate's threshold, so a stale threshold shows.
        const th = psx.getBits(cand, "threshold");
        cand.score = @as(i32, ctx.value(u3)) - 3 + @as(i32, @intCast(th & 1));
        const w: u8 = if (ctx.value(u1) == 1) ctx.value(u3) else ctx.value(u8);
        psx.setBits(cand, "weight", w);
    }

    pub fn func_800DC2E0(ctx: *zd.Ctx, dst: anytype, src: anytype) void {
        const d: [*]u8 = @ptrCast(@as(?*anyopaque, dst).?);
        const s: [*]const u8 = @ptrCast(@as(?*const anyopaque, src).?);
        @memcpy(d[0..20], s[0..20]);
        // The copy leaves its own threshold: the C sets list[1].threshold after
        // copying, and a port that does not makes it observable here.
        const dc: *v.actionCandidateEntry = @ptrCast(@alignCast(@as(?*anyopaque, dst).?));
        psx.setBits(dc, "threshold", ctx.value(u16));
    }

    pub fn func_800DC344(ctx: *zd.Ctx, state: anytype, candidate: anytype) void {
        _ = candidate;
        const st: *v.actionChoiceState = @ptrCast(@alignCast(@as(?*anyopaque, state).?));
        st.unk154[@as(usize, ctx.value(u8)) % 220] = ctx.value(u8);
    }

    pub fn func_800DC4F0(ctx: *zd.Ctx, candidate: anytype, info: anytype) void {
        _ = info;
        const cand: *v.actionCandidateEntry = @ptrCast(@alignCast(@as(?*anyopaque, candidate).?));
        if (ctx.value(u1) == 1) cand.score = @as(i32, ctx.value(u3)) - 3;
    }

    pub fn func_800E4CF4(ctx: *zd.Ctx, state: anytype, candidate: anytype, id: anytype) c_int {
        _ = state;
        _ = candidate;
        _ = id;
        return if (ctx.value(u2) == 0) 0 else 1;
    }

    pub fn func_800DC484(ctx: *zd.Ctx, action: anytype, id: anytype) c_int {
        _ = action;
        _ = id;
        return ctx.value(c_int);
    }

    pub fn func_800DC48C(ctx: *zd.Ctx, action: anytype, id: anytype) c_int {
        _ = action;
        _ = id;
        return ctx.value(c_int);
    }

    pub fn rand(ctx: *zd.Ctx) c_int {
        if (ctx.value(u1) == 1) return @intCast(ctx.value(u3));
        return @intCast(ctx.value(u31));
    }
};
