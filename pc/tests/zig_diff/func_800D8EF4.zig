//! zig_diff spec: func_800D8EF4 -> func_800D8EF4.func_800D8EF4 (6FC00.c).
//! Inputs: the battle actor list (0 to 15 nodes, ids 1..15 with the first
//! one sometimes 0), each with flags 0x20/0x40/0x80 drawn, stats whose
//! currentHP/maxHP sit at the 2x and 4x edges, and a state in the
//! scratchpad table with its counters at the wrap and compare edges
//! (unk46 at 2, unkFA at 4, unk38 at 0 and in the 0..16 object-index
//! range). Objects come from a small pool that the D_800F4538 table also
//! points into, with null entries. Stubs write back what the C reads
//! afterwards (D_800F58D4, unk18C, unk2F, unk12A, unk40, unk156, unk114,
//! unk131) and return zero or nonzero values. func_800E45B4 sometimes returns
//! D_800F16F4 itself so the "arg == limit" edge occurs.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_6FC00");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D8EF4));

pub const classes = [_][:0]const u8{ "no-actors", "first-skipped", "processed" };

var last_class: usize = 0;

fn pickOne(ctx: *zd.Ctx, comptime T: type, edges: []const T) T {
    return edges[ctx.random.uintLessThan(usize, edges.len)];
}

fn pickObj(ctx: *zd.Ctx, objs: *const [4]*v.actorObject) *v.actorObject {
    return objs[ctx.random.uintLessThan(usize, 4)];
}

fn asState(p: anytype) *v.actorUpdateState {
    return @ptrCast(@alignCast(p));
}

fn setupObject(ctx: *zd.Ctx, o: *v.actorObject) void {
    o.tile.raw = ctx.int(u32);
    psx.setBits(&o.flags.bits, "unk16", if (ctx.chance(3)) @as(u3, 0) else ctx.int(u3));
    psx.setBits(&o.flags.bits, "unk19", if (ctx.chance(3)) @as(u2, 0) else ctx.int(u2));
    psx.setBits(&o.flags.bits, "unk31", if (ctx.chance(2)) @as(u1, 1) else @as(u1, 0));
    psx.setBits(o, "unk5AC_15", if (ctx.chance(3)) @as(u1, 1) else @as(u1, 0));
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const bc = ctx.alloc(v.D_800F58BC_t);
    bc.unk18 = pickOne(ctx, u16, &.{ 0, 1, 0xFFFD, 0xFFFE, 0xFFFF });
    v.g.D_800F58BC().* = bc;
    v.g.D_800F16F4().* = pickOne(ctx, i32, &.{ 0, 1, -1, 0x7FFF_FFFF, -0x8000_0000 });
    v.g.D_800F58D4().* = ctx.int(i32);

    var objs: [4]*v.actorObject = undefined;
    for (&objs) |*o| {
        o.* = ctx.alloc(v.actorObject);
        setupObject(ctx, o.*);
    }
    const dtab = v.g.D_800F4538();
    for (dtab) |*e| {
        e.* = if (ctx.chance(4)) null else @as(*v.D_800F4538_t, @ptrCast(@alignCast(pickObj(ctx, &objs))));
    }

    const n: usize = if (ctx.chance(10)) 0 else ctx.range(usize, 1, 15);
    var nodes: [15]*v.vs_battle_actor = undefined;
    for (0..n) |i| {
        const node = ctx.alloc(v.vs_battle_actor);
        nodes[i] = node;
        const stats = ctx.alloc(v.vs_battle_actor2);
        const st = ctx.alloc(v.actorUpdateState);
        const p60 = ctx.alloc(v.actorUpdatePoint);
        const p64 = ctx.alloc(v.actorUpdatePoint);

        const id: usize = if (i == 0 and ctx.chance(6)) 0 else i + 1;
        node.id = @intCast(id);
        var f: u8 = ctx.int(u8) & 0x1F;
        if (ctx.chance(6)) f |= 0x20;
        if (ctx.chance(5)) f |= 0x80;
        if (ctx.chance(4)) f |= 0x40;
        node.unk8 = f;
        node.unk9 = pickOne(ctx, u8, &.{ 8, 9, 10, 13, 16, 0, 4, 31 });
        psx.put(&node.stats, stats);

        const cur: u16 = if (ctx.chance(4)) pickOne(ctx, u16, &.{ 0, 0xFFFF, 0x8000 }) else ctx.range(u16, 0, 0x3FFF);
        stats.currentHP = @bitCast(cur);
        const mx: u16 = switch (ctx.random.uintLessThan(u8, 4)) {
            0 => cur *% 4,
            1 => cur *% 2,
            2 => ctx.int(u16),
            else => pickOne(ctx, u16, &.{ 0, 1, 0xFFFF }),
        };
        stats.maxHP = @bitCast(mx);
        stats.unk954 = ctx.int(u16);

        psx.put(&st.actor, node);
        psx.put(&st.object, pickObj(ctx, &objs));
        psx.put(&st.unk60, p60);
        psx.put(&st.unk64, p64);

        const top: u32 = if (ctx.chance(4)) 0x80 + ctx.range(u32, 0, 0x7F) else ctx.range(u32, 0, 16);
        p64.position = (top << 24) | (ctx.int(u32) & 0x00FF_FFFF);
        p64.unk4 = ctx.int(u32);
        p60.position = ctx.int(u32);
        p60.unk4 = ctx.int(u32);

        st.unk34 = if (ctx.chance(2))
            (p64.position & 0x00FF_00FF) | (ctx.int(u32) & 0xFF00_FF00)
        else
            ctx.int(u32);
        const ref = pickObj(ctx, &objs).tile.raw;
        st.unk3C = if (ctx.chance(2))
            (ref & 0x00FF_00FF) | (ctx.int(u32) & 0xFF00_FF00)
        else
            ctx.int(u32);

        st.unk18 = pickOne(ctx, u8, &.{ 0, 1 });
        st.unk12A = pickOne(ctx, u8, &.{ 0, 1, 2 });
        st.unk138 = pickOne(ctx, u16, &.{ 5, 6, 7 });
        st.unk19A = pickOne(ctx, u8, &.{ 0, 1, 2 });
        st.unk46 = pickOne(ctx, u16, &.{ 0, 1, 2, 0xA8C });
        st.unk45 = pickOne(ctx, u8, &.{ 0, 1, 2, 40 });
        st.unkFA = pickOne(ctx, u8, &.{ 0, 3, 3, 4, 5, 255 });
        st.unk1C = pickOne(ctx, u8, &.{ 0, 1 });
        st.unk124 = pickOne(ctx, u8, &.{ 0, 8, 9, 13, 16, 31 });
        st.unk2F = pickOne(ctx, u8, &.{ 0, 1, 2, 3 });
        st.unk114 = pickOne(ctx, u16, &.{ 0, 1 });
        st.unk131 = pickOne(ctx, u8, &.{ 0, 2 });
        st.unk156 = pickOne(ctx, u16, &.{ 0, 1 });
        st.unk40 = pickOne(ctx, u16, &.{ 0, 1, 2 });
        st.unk18C = if (ctx.chance(3)) 1 else 0;
        st.unkE = ctx.int(u8);
        st.historyIndex = ctx.range(u8, 0, 7);
        st.historyMask = ctx.int(u8);
        if (ctx.chance(3)) {
            // A stalled history: every slot written and both halves repeating.
            st.historyMask = 0xFF;
            const hx: *[8]u8 = @ptrCast(&st.historyX);
            const hz: *[8]u8 = @ptrCast(&st.historyZ);
            @memcpy(hx[4..8], hx[0..4]);
            @memcpy(hz[4..8], hz[0..4]);
        }
        psx.setBits(st, "unk190_5", pickOne(ctx, u7, &.{ 0, 1, 5, 127 }));
        // Half the states have no alerting target (bit 5 with mode 3), so the
        // scan over the actor list also runs to its end.
        const calm = ctx.chance(2);
        for (&st.targets) |*t| {
            t.* = if (!calm and ctx.chance(3)) @as(u32, 0xE0) else pickOne(ctx, u32, &.{ 0, 0x20, 0x40, 0xC0, 0x80 });
        }

        st.unk38.b[0] = ctx.int(u8);
        st.unk38.b[1] = ctx.int(u8);
        st.unk38.b[2] = ctx.int(u8);
        st.unk38.b[3] = switch (ctx.random.uintLessThan(u8, 4)) {
            0 => 0,
            1 => ctx.range(u8, 0, 16),
            2 => 0x80 | ctx.range(u8, 0, 0x7F),
            else => 0xFF,
        };
        if (st.unk38.b[3] == 0 and ctx.chance(2)) {
            st.unk38.b[0] = 0;
            st.unk38.b[1] = 0;
            st.unk38.b[2] = 0;
        }

        const slot: *align(1) u32 = @ptrFromInt(0x1F80_037C + 4 * id);
        slot.* = psx.addr(st);
    }
    for (0..n) |i| {
        psx.put(&nodes[i].next, if (i + 1 < n) nodes[i + 1] else null);
    }
    v.g.vs_battle_actors()[0] = if (n > 0) nodes[0] else null;

    last_class = if (n == 0) 0 else if ((nodes[0].unk8 & 0x20) != 0) 1 else 2;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return last_class;
}

pub const stubs = struct {
    pub fn func_800E45B4(ctx: *zd.Ctx) c_int {
        if (ctx.value(u2) == 0) return v.g.D_800F16F4().*;
        return ctx.value(c_int);
    }

    pub fn func_800D8280(ctx: *zd.Ctx, state: anytype) void {
        asState(state.?).unk18C = if (ctx.value(u3) == 0) 1 else 0;
    }

    pub fn func_800E4358(ctx: *zd.Ctx, state: anytype) void {
        asState(state.?).unk12A = ctx.value(u2);
    }

    pub fn func_800DBFE4(ctx: *zd.Ctx, state: anytype) void {
        asState(state.?).unk114 = ctx.value(u1);
    }

    pub fn func_800D87E8(ctx: *zd.Ctx, state: anytype, id: anytype) void {
        _ = id;
        const s = asState(state.?);
        s.unk2F = ctx.value(u2);
        v.g.D_800F58D4().* = ctx.value(u2);
    }

    pub fn func_800E4F14(ctx: *zd.Ctx, state: anytype) void {
        asState(state.?).unk156 = ctx.value(u2);
    }

    pub fn func_800D85D8(ctx: *zd.Ctx, state: anytype) c_int {
        _ = state;
        if (ctx.value(u2) == 0) return 0;
        return @as(c_int, ctx.value(u30)) + 1;
    }

    pub fn func_800E4CE8(ctx: *zd.Ctx, state: anytype) void {
        asState(state).unk40 = ctx.value(u4);
    }

    pub fn func_800E2CCC(ctx: *zd.Ctx, state: anytype) void {
        asState(state.?).unk131 = ctx.value(u2);
    }

    pub fn func_800E48A8(ctx: *zd.Ctx, state: anytype, a: anytype, b: anytype) void {
        _ = a;
        _ = b;
        asState(state.?).unk2F = ctx.value(u2);
    }
};
