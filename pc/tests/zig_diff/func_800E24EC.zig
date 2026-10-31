//! zig_diff spec: func_800E24EC -> func_800E24EC.func_800E24EC (771A8.c).
//! Inputs: a targetMovementState in the arena with a target 0-15 (sometimes
//! 16, which the 17-entry D_800F4538 table allows but targets[] and distances[]
//! do not), its own id equal or not, a model per table entry whose flags pick
//! the tile test or the destination test, tile coordinates that match or miss,
//! the range at the squared-distance boundaries (including 46341, where
//! value * value overflows), distances equal to each candidate square so the
//! unsigned comparisons see equal sides, and the option argument around 0 and 1
//! (forced to 1 when the s->model flags have 0x200000). Most inputs keep the
//! movement state (same range, target and option as last time, destination on
//! the target's tile, small timer), so the reset test is false and bE7/bE8
//! survive into the rangeMode tests. The streak flags are 0, 1, 127, 128 or 255,
//! so a positive value with the sign bit clear occurs. The actor id is 8, 9 or
//! 10 often enough for the `>= 9` test to see equal sides. The stubs write the
//! state the C reads back (flags15E, tile, destination, timeout, bE4, bE8) and
//! return zero and nonzero where the C tests a result.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_771A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E24EC));

pub const classes = [_][:0]const u8{ "self", "no-f0", "f0-no-f2", "mode-0", "mode-1", "mode-2", "mode-other" };

/// A streak flag: zero, or a positive byte, or one with the sign bit set.
fn flag(ctx: *zd.Ctx) u8 {
    return ctx.edge(u8, &.{ 0, 1, 1, 127, 128, 255 });
}

fn sqr(x: i32) u32 {
    return @bitCast(x *% x);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const s = ctx.alloc(v.targetMovementState);
    _ = ctx.allocBytes(64, 16); // room for distances[16] of target 16

    const tbl = v.g.D_800F4538();
    var models: [17]*v.targetMovementModel = undefined;
    for (0..17) |i| {
        models[i] = ctx.alloc(v.targetMovementModel);
        const p: *v.D_800F4538_t = @ptrCast(models[i]);
        tbl[i] = p;
    }

    const target: u8 = if (ctx.chance(12)) 16 else ctx.range(u8, 0, 15);
    s.targetId = target;
    s.id = if (ctx.chance(6)) target else ctx.int(u8);

    // Most inputs keep the movement state, so the reset test stays false.
    const stay = !ctx.chance(4);

    // The s->model object (psx_get(s->model)->flags); 0x200000 forces option to 1.
    const sm = ctx.alloc(v.targetMovementModel);
    sm.flags = if (ctx.chance(2)) 0x200000 else ctx.int(u32) & ~@as(u32, 0x200000);
    psx.put(&s.model, sm);
    const option: c_int = if ((sm.flags & 0x200000) != 0) 1 else ctx.pick(c_int, &.{ 0, 1, 2, -1 });

    // The model the C reads through D_800F4538[target].
    const m = models[target];
    m.flags = if (stay)
        ctx.int(u32) | 0x10000
    else if (ctx.chance(2))
        ctx.int(u32) & ~@as(u32, 0x1F0000)
    else
        ctx.int(u32) | 0x10000;
    s.targetTileX = ctx.int(u8);
    s.targetTileZ = ctx.int(u8);
    const tx: i32 = s.targetTileX;
    const tz: i32 = s.targetTileZ;
    const mx: i16 = @intCast(tx * 64 + ctx.range(i32, 0, 63));
    const mz: i16 = @intCast(tz * 64 + ctx.range(i32, 0, 63));
    m.x = if (ctx.chance(2)) mx else ctx.int(i16);
    m.z = if (ctx.chance(2)) mz else ctx.int(i16);

    // The s->actor object (psx_get(s->actor)->id).
    const actor = ctx.alloc(v.targetMovementActor);
    actor.id = ctx.pick(u8, &.{ 8, 9, 10, 0, 255 });
    psx.put(&s.actor, actor);

    s.range = switch (ctx.range(u8, 0, 4)) {
        0 => ctx.edge(i32, &.{ 0, 31, 32, 33, 95, 96, 97, 127, 128, 129 }),
        1 => ctx.range(i32, -300, 300),
        2 => ctx.edge(i32, &.{ 46340, 46341 }),
        else => ctx.range(i32, 0, 255),
    };
    const r = s.range;
    s.previousRange = if (stay or ctx.chance(4)) r else ctx.int(i32);
    s.previousTargetId = if (stay or ctx.chance(4)) target else ctx.int(u8);
    s.previousOption = if (stay or ctx.chance(4)) @truncate(@as(u32, @bitCast(option))) else ctx.int(u8);

    // distances[target]: equal to one of the squares the C compares sometimes.
    const cands = [_]u32{ sqr(r -% 96), sqr(r), sqr(r -% 128), sqr(r -% 32) };
    const dist: u32 = if (ctx.chance(3))
        cands[ctx.range(u8, 0, 3)]
    else
        ctx.pick(u32, &.{ 0, 1, 0xFFFF_FFFF, 0x7FFF_FFFF, 16384 });
    psx.at(&s.distances, target).* = dist;

    const tf = psx.at(&s.targets, target);
    psx.setBits(tf, "f0", ctx.int(u1));
    psx.setBits(tf, "f1", ctx.int(u1));
    psx.setBits(tf, "f2", ctx.int(u1));
    psx.setBits(tf, "f5", ctx.int(u1));

    s.destination.raw = ctx.int(u32);
    s.destination.b[0] = if (stay or ctx.chance(2)) s.targetTileX >> 1 else ctx.int(u8);
    s.destination.b[2] = if (stay or ctx.chance(2)) s.targetTileZ >> 1 else ctx.int(u8);
    s.tile.raw = if (ctx.chance(3)) s.destination.raw else ctx.int(u32);
    s.cachedDestination.raw = if (ctx.chance(2)) s.tile.raw else ctx.int(u32);

    s.timer = if (stay)
        ctx.edge(u16, &.{ 0, 1, 2, 180 })
    else
        ctx.pick(u16, &.{ 0, 180, 181, 0xFFFF });
    s.timeout = if (ctx.chance(2)) 0 else ctx.int(u8);
    s.bE4 = flag(ctx);
    s.bE7 = flag(ctx);
    s.bE8 = flag(ctx);
    s.b24 = flag(ctx);
    s.b33 = flag(ctx);
    s.flags15E = ctx.int(u16);
    s.rangeMode = if (ctx.chance(2)) ctx.range(u8, 0, 2) else ctx.int(u8);

    return .{ s, option };
}

pub fn classify(args: Args) usize {
    const s = args[0].?;
    if (s.targetId == s.id) return 0;
    const tf = psx.at(&s.targets, s.targetId);
    if (psx.getBits(tf, "f0") == 0) return 1;
    if (psx.getBits(tf, "f2") == 0) return 2;
    return switch (s.rangeMode) {
        0 => 3,
        1 => 4,
        2 => 5,
        else => 6,
    };
}

/// The state pointer as a typed view, whether the C passes it optional or not.
fn st(p: anytype) *v.targetMovementState {
    const q = if (@typeInfo(@TypeOf(p)) == .optional) p.? else p;
    return @ptrCast(@alignCast(q));
}

pub const stubs = struct {
    pub fn ratan2(ctx: *zd.Ctx, y: anytype, x: anytype) c_int {
        _ = y;
        _ = x;
        return ctx.value(c_int);
    }

    pub fn func_800DEEFC(ctx: *zd.Ctx, p: anytype, option: anytype) void {
        _ = option;
        if (ctx.value(u2) == 0) st(p).timer = ctx.value(u16);
    }

    pub fn func_800E2CCC(ctx: *zd.Ctx, p: anytype) void {
        if (ctx.value(u1) == 1) st(p).movementTargetId = ctx.value(u8);
    }

    pub fn func_800D8260(ctx: *zd.Ctx, p: anytype, a: anytype, b: anytype) void {
        _ = a;
        _ = b;
        if (ctx.value(u1) == 1) st(p).bE8 = ctx.value(u8);
    }

    pub fn func_800E4660(ctx: *zd.Ctx, a: anytype, b: anytype, c3: anytype) c_uint {
        _ = a;
        _ = b;
        _ = c3;
        return @as(c_uint, ctx.value(u16));
    }

    pub fn func_800E50A0(ctx: *zd.Ctx, p: anytype, mode: anytype, angle: anytype) void {
        _ = mode;
        _ = angle;
        const s = st(p);
        if (ctx.value(u1) == 1) s.destination.raw = ctx.value(u32);
        if (ctx.value(u2) == 0) s.bE4 = ctx.value(u8);
    }

    pub fn func_800E1388(ctx: *zd.Ctx, p: anytype, tile: anytype, angle: anytype, distance: anytype) void {
        _ = tile;
        _ = angle;
        _ = distance;
        const s = st(p);
        s.tile.raw = if (ctx.value(u2) == 0) s.destination.raw else ctx.value(u32);
    }

    pub fn func_800E4FB0(ctx: *zd.Ctx, p: anytype, target: anytype, range: anytype) c_int {
        _ = target;
        _ = range;
        if (ctx.value(u2) == 0) st(p).flags15E = ctx.value(u16);
        return ctx.value(c_int);
    }

    pub fn func_800E4180(ctx: *zd.Ctx, p: anytype, a: anytype, b: anytype, c3: anytype) c_int {
        _ = a;
        _ = b;
        _ = c3;
        if (ctx.value(u1) == 1) st(p).timeout = ctx.value(u8);
        if (ctx.value(u8) < 110) return 0;
        return ctx.value(c_int);
    }

    pub fn func_800E4904(ctx: *zd.Ctx, p: anytype) void {
        if (ctx.value(u1) == 1) st(p).bD = ctx.value(u8);
    }
};
