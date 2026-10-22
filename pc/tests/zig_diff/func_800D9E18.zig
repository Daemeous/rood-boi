//! zig_diff spec: func_800D9E18 -> func_800D9E18.func_800D9E18 (71618.c).
//! Inputs: a candidateMovementState and an actionCandidateEntry in the arena,
//! a second state (the height source for D_800F5878), one actor shared by the
//! D_800F4538 table, a D_800F58BC block, and the scratchpad slot table at
//! 0x1F80037C (zero or not per id). Four lanes steer the input toward the
//! edges the C compares: the minimum clamp (lane 2: flags bit 8 clear, maxRange
//! 0, distanceSquared 0, so limit is the square of the rsqrt of 0), the case-2
//! range test with equal sides (lane 3: the callee returns the entry's own
//! distanceSquared, so both rsqrt calls take the same input), the
//! distances-versus-limit test with limit 0x3E04000 (lane 4, flags bit 8 set),
//! and mode 7 with targets[0] in class 3 (lane 5). Counts of 0, -1 and INT_MAX,
//! every mode, own-class and other targets, and both results of each callee are
//! reached. vs_gte_rsqrt is stubbed as the integer square root. The callee stubs write the fields the C reads back (distanceSquared,
//! flag1, destination, targets[0], distances) and return 0/1 or the edge values
//! the C tests (0xFFFF for the `<= 0xFFFF` test).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_71618");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D9E18));

pub const classes = [_][:0]const u8{ "count-zero", "no-slot", "mode1-own", "mode1", "mode2", "mode4", "mode7", "other-mode" };

/// The distanceSquared of the entry in lane 3: func_800E4624 returns it, so
/// both sides of the case-2 range test are rsqrt of the same value.
var eq_target: c_int = 0;

fn bit(ctx: *zd.Ctx) c_int {
    return @intCast(ctx.value(u1));
}

pub const stubs = struct {
    /// A deterministic root (the integer square root of the input as unsigned),
    /// so the lanes' equal sides and the clamp's edge happen as planned.
    pub fn vs_gte_rsqrt(ctx: *zd.Ctx, x: anytype) c_int {
        _ = ctx;
        return @intCast(std.math.sqrt(@as(u32, @bitCast(@as(c_int, x)))));
    }

    pub fn func_800D82CC(ctx: *zd.Ctx, p: anytype) void {
        const s: *v.candidateMovementState = @ptrCast(@alignCast(p));
        if (ctx.value(u2) == 0) s.targets[0].raw = ctx.value(u32);
    }

    pub fn func_800D836C(ctx: *zd.Ctx, p: anytype) void {
        const s: *v.candidateMovementState = @ptrCast(@alignCast(p));
        if (ctx.value(u1) == 1) s.distances[ctx.value(u4)] = ctx.value(u32);
    }

    pub fn func_800DB8AC(ctx: *zd.Ctx, state: anytype, entry: anytype) c_int {
        _ = state;
        const e: *v.actionCandidateEntry = @ptrCast(@alignCast(entry));
        if (ctx.value(u2) == 0) e.distanceSquared = ctx.value(u32);
        return bit(ctx);
    }

    pub fn func_800DBCB4(ctx: *zd.Ctx, state: anytype, entry: anytype) c_int {
        const s: *v.candidateMovementState = @ptrCast(@alignCast(state));
        const e: *v.actionCandidateEntry = @ptrCast(@alignCast(entry));
        if (ctx.value(u2) == 0) psx.setBits(e, "flag1", ctx.value(u1));
        if (ctx.value(u2) == 0) s.destination.raw = ctx.value(u32);
        return bit(ctx);
    }

    pub fn func_800E42D4(ctx: *zd.Ctx, x: anytype, z: anytype) c_int {
        _ = .{ x, z };
        return @as(c_int, ctx.value(i16));
    }

    pub fn func_800E4624(ctx: *zd.Ctx, point: anytype, pos: anytype, h1: anytype, h2: anytype) c_int {
        _ = .{ point, pos, h1, h2 };
        if (ctx.value(u1) == 0) return eq_target;
        return ctx.value(c_int);
    }

    pub fn func_800E4660(ctx: *zd.Ctx, x: anytype, y: anytype, z: anytype) c_uint {
        _ = .{ x, y, z };
        return switch (ctx.value(u3)) {
            0 => 0xFFFF,
            1 => 0xFFFE,
            2 => 0x10000,
            3 => 0,
            else => ctx.value(c_uint),
        };
    }

    pub fn func_800DB61C(ctx: *zd.Ctx, state: anytype, k: anytype) c_int {
        _ = .{ state, k };
        return ctx.value(c_int);
    }

    pub fn func_800DB370(ctx: *zd.Ctx, state: anytype, id: anytype, distance: anytype, flag: anytype) c_int {
        _ = .{ state, id, distance, flag };
        return bit(ctx);
    }

    pub fn func_800DB74C(ctx: *zd.Ctx, state: anytype, id: anytype) c_int {
        _ = .{ state, id };
        return bit(ctx);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.alloc(v.candidateMovementState);
    const entry = ctx.alloc(v.actionCandidateEntry);
    const other = ctx.alloc(v.candidateMovementState);
    const actor = ctx.alloc(v.D_800F4538_t);
    const hud = ctx.alloc(v.D_800F58BC_t);

    v.g.D_800F58BC().* = hud;
    for (v.g.D_800F4538()) |*p| p.* = actor;
    const callee = v.g.D_800F5878();
    for (0..18) |i| callee[i] = @ptrCast(other);
    for (0..16) |i| {
        const slot: *u32 = @ptrFromInt(0x1F80_037C + 4 * i);
        slot.* = if (ctx.chance(5)) 0 else (ctx.int(u32) | 1);
    }

    psx.setBits(entry, "targetId", ctx.int(u4));
    psx.setBits(entry, "flag1", ctx.int(u1));
    psx.setBits(entry, "range", if (ctx.chance(3)) 0 else ctx.int(u16));
    entry.distanceSquared = ctx.pick(u32, &.{ 0, 1, 0xFFFF_FFFF, 0x7FFF_FFFF, 0x8000_0000 });

    state.count = if (ctx.chance(12)) 0 else ctx.pick(i32, &.{ -1, 1, 2, std.math.maxInt(i32), std.math.minInt(i32) });
    state.flags = ctx.pick(u16, &.{ 0, 8, 0xFFFF, 0x8000, 0xFFF7 });
    state.maxRange = ctx.pick(u8, &.{ 0, 1, 20, 255 });
    state.height = ctx.pick(i16, &.{ 0, -1, 32767, -32768 });
    other.height = ctx.pick(i16, &.{ 0, -1, 32767, -32768 });

    const lane = ctx.range(u3, 0, 7);
    state.mode = switch (lane) {
        2, 4 => 1,
        3, 7 => 2,
        5 => 7,
        6 => 4,
        else => ctx.pick(u8, &.{ 1, 2, 4, 7, 0, 3, 8, 255 }),
    };
    switch (lane) {
        2 => {
            // limit = (rsqrt(0) + 0)^2 is below 0x64000 when the rsqrt of 0 is
            // small: the minimum clamp is taken with the non-active result.
            state.flags &= ~@as(u16, 8);
            state.maxRange = 0;
            entry.distanceSquared = ctx.pick(u32, &.{ 0, 640 * 640, 640 * 640, 639 * 639 }); // 640^2: limit squared is exactly the 0x64000 minimum
            state.mode = 1;
        },
        3 => {
            // The callee returns distanceSquared itself (eq_target), so the
            // lhs rsqrt and the rhs rsqrt(distanceSquared) - range are equal.
            const d = ctx.int(u32);
            entry.distanceSquared = d;
            eq_target = @bitCast(d);
            psx.setBits(entry, "flag1", 1);
            psx.setBits(entry, "range", 0);
        },
        4 => {
            // limit = 0x1F80 squared = 0x3E04000: distances[id] == limit, every target in class 2.
            state.flags |= 8;
            for (&state.distances) |*d| d.* = 0x3E04000;
            for (&state.targets) |*t| t.bytes[0] = (t.bytes[0] & 0x3F) | 0x80;
        },
        5 => {
            state.targets[0].bytes[0] = (state.targets[0].bytes[0] & 0x3F) | 0xC0;
        },
        else => {},
    }
    if (lane >= 2 and state.count == 0) state.count = -1;

    return .{ state, entry };
}

pub fn classify(args: Args) usize {
    const state = args[0].?;
    const entry = args[1].?;
    if (state.count == 0) return 0;
    const id: usize = psx.getBits(entry, "targetId");
    const slot: *u32 = @ptrFromInt(0x1F80_037C + 4 * id);
    if (slot.* == 0) return 1;
    return switch (state.mode) {
        1 => if (state.targets[id].bytes[0] >> 6 == 2) 2 else 3,
        2 => 4,
        4 => 5,
        7 => 6,
        else => 7,
    };
}
