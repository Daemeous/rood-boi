//! zig_diff spec: func_800E1EAC -> func_800E1EAC.func_800E1EAC (771A8.c).
//! Inputs: a wander state whose object picks either direction check
//! (flags bit 21); the first-rest mode (mode12D 1) with the timer around
//! 31; the timer one short of, at and past the move period and rest + move;
//! a range below, at and above 63; a rate 0-40 (the C's shift masks 32+);
//! modes 0-3 and above; cooldown 0, 1 or any; flag8 and flags bit 3 both
//! ways; the goal on the current tile or not; map bounds a tile around the
//! actor's, so each edge test meets its side, and a collision grid (the
//! scratchpad's 0x1F8003C0 word) with the 0x800 wall bit on or off. The
//! RNG stub (func_800E45D4) returns 0, 43, 44, 45 or any; the direction
//! checks pass a quarter of the time (all four fail too); the destination
//! search (func_800E1BB8) returns -1, 0, 1 or any.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_771A8");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E1EAC));

pub const classes = [_][:0]const u8{ "first-rest", "roam", "walk" };

fn rng(ctx: *zd.Ctx) c_int {
    return switch (ctx.value(u3)) {
        0 => 0,
        1 => 43,
        2 => 44,
        3 => 45,
        else => ctx.value(c_int),
    };
}

pub const stubs = struct {
    pub fn func_800E45D4(ctx: *zd.Ctx, n: anytype) c_int {
        _ = n;
        return rng(ctx);
    }
    pub fn func_800E0678(ctx: *zd.Ctx, s: anytype, d: anytype) c_int {
        _ = s;
        _ = d;
        return if (ctx.value(u2) == 0) ctx.value(c_int) | 1 else 0;
    }
    pub fn D_800D9B6C(ctx: *zd.Ctx, s: anytype, d: anytype) c_int {
        _ = s;
        _ = d;
        return if (ctx.value(u2) == 0) ctx.value(c_int) | 1 else 0;
    }
    pub fn func_800E1BB8(ctx: *zd.Ctx, s: anytype, tile: anytype, count: anytype, arg3: anytype) c_int {
        _ = s;
        _ = tile;
        _ = count;
        _ = arg3;
        return switch (ctx.value(u2)) {
            0 => -1,
            1 => 0,
            2 => 1,
            else => ctx.value(c_int),
        };
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const s = ctx.alloc(v.wanderState);
    const object = ctx.alloc(v.wanderObject);
    psx.put(&s.object, object);
    object.flags = if (ctx.chance(2)) ctx.int(u32) | 0x200000 else ctx.int(u32) & ~@as(u32, 0x200000);

    s.mode12D = if (ctx.chance(4)) 1 else ctx.int(u8);
    s.movePeriod = ctx.pick(u16, &.{ 0, 1, 30, 31, 32, 100 });
    s.restPeriod = ctx.pick(u16, &.{ 0, 1, 30, 100 });
    const move = s.movePeriod;
    const rest = s.restPeriod;
    s.timer = switch (ctx.range(u8, 0, 6)) {
        0 => move -% 1,
        1 => move -% 2,
        2 => move,
        3 => rest +% move -% 1,
        4 => rest +% move,
        5 => ctx.range(u16, 28, 32),
        else => ctx.int(u16),
    };
    s.rate = if (ctx.chance(8)) ctx.range(u8, 32, 40) else ctx.range(u8, 0, 12);
    s.mode = ctx.pick(u16, &.{ 0, 1, 2, 3 });
    s.cooldown = ctx.pick(u16, &.{ 0, 1, 60 });
    s.flag8 = if (ctx.chance(2)) 0 else ctx.int(u8) | 1;
    s.flags = ctx.int(u16);
    s.angle = ctx.int(i16);

    // The tile near the map's bounds, the grid big enough for any x, z the bounds allow.
    psx.setBits(&s.tile.p, "x", ctx.range(u8, 0, 6));
    psx.setBits(&s.tile.p, "z", ctx.range(u8, 0, 6));
    s.goal = if (ctx.chance(3)) s.tile else .{ .raw = ctx.int(u32) };
    if (ctx.chance(3)) {
        // The same x as the tile, another z (the arrival test must compare both).
        s.goal = s.tile;
        psx.setBits(&s.goal.p, "z", psx.getBits(&s.tile.p, "z") +% 1);
    }
    const bounds = ctx.alloc(v.D_800F58BC_t);
    v.g.D_800F58BC().* = bounds;
    bounds.maxX = ctx.range(u8, 0, 8);
    bounds.maxZ = ctx.range(u8, 0, 8);
    bounds.minX = ctx.range(u8, 0, 6);
    bounds.minZ = ctx.range(u8, 0, 6);
    const grid = ctx.alloc([16][32]u16);
    for (grid) |*row| {
        for (row) |*t| t.* = if (ctx.chance(4)) 0x800 else ctx.int(u16) & ~@as(u16, 0x800);
    }
    @as(*u32, @ptrFromInt(0x1F8003C0)).* = psx.addr(grid);

    const range: c_uint = ctx.pick(c_uint, &.{ 1, 3, 62, 63, 64, 200 });
    const tile: c_uint = ctx.int(c_uint);
    return .{ s, tile, if (range == 0) 1 else range };
}

pub fn classify(args: Args) usize {
    const s = args[0].?;
    if (s.mode12D == 1) return 0;
    return if (args[2] >= 63) 1 else 2;
}
