//! zig_diff spec: func_800E0A68 -> func_800E0A68.func_800E0A68 (771A8.c).
//! Inputs: a step state near its tile and destination (tiles 0-3, the
//! position within a step of the tile centre, the height small), so each
//! distance test meets the step (0-12) at, below and above it; the object's
//! flying bit both ways; the ledge case (unk0, a near tile, the grid's 0x40
//! bit) with the actor's height and D_800F58BC->unk14 around the rise; the
//! destination reached (the grid's 0x20 bit, the actor on it or not); the
//! wall (5) and obstacle (7) results of func_800E3600 (and another). Every
//! height-like callee returns a small value (the ceiling sometimes far
//! below: the arc's int product then wraps); vs_gte_rsqrt gives a distance
//! of 9-24 or the step itself, and a denominator of 0 (the C's `!denom`),
//! 2, 34096 (a quotient of 1) or 4098-4113.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_771A8");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E0A68));

pub const classes = [_][:0]const u8{ "ground", "ledge", "arrive", "wall", "obstacle" };

var last_step: c_int = 0;
/// vs_gte_rsqrt's calls in this run (the distance, then the denominator).
var rsqrt_calls: u32 = 0;
var kind: usize = 0;

/// The obstacle arc divides by its height + ground: there every height is
/// non-negative and the floor rise at least 1 (the C traps on 0).
var arc = false;
var floor_at_py: c_int = 0;

fn small(ctx: *zd.Ctx) c_int {
    return if (arc) @as(c_int, ctx.value(u5)) else @as(c_int, ctx.value(u5)) - 8;
}

pub const stubs = struct {
    pub fn func_800E0918(ctx: *zd.Ctx, s: anytype, a: anytype, b: anytype) c_int {
        rsqrt_calls = 0;
        _ = s;
        _ = a;
        _ = b;
        return if (ctx.value(u4) == 0) 1 else 0;
    }
    pub fn func_800E4764(ctx: *zd.Ctx, s: anytype, step: anytype, mode: anytype) c_int {
        _ = s;
        const st: *c_int = @ptrCast(@alignCast(step));
        const md: *c_int = @ptrCast(@alignCast(mode));
        st.* = @as(c_int, ctx.value(u4) % 13);
        md.* = ctx.value(c_int);
        last_step = st.*;
        return if (ctx.value(u4) == 0) 0 else 1;
    }
    pub fn func_800E45F4(ctx: *zd.Ctx, a: anytype, b: anytype) c_uint {
        _ = a;
        _ = b;
        return ctx.value(u2);
    }
    pub fn func_800E42E0(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        return small(ctx) + @intFromBool(arc);
    }
    pub fn func_800E6A6C(ctx: *zd.Ctx, a: anytype, b: anytype, x: anytype, z: anytype) c_int {
        _ = a;
        _ = b;
        _ = x;
        _ = z;
        // The floor at the actor's height sometimes (the ceiling test's `ground < py` at equality).
        if (ctx.value(u2) == 0) return floor_at_py;
        return small(ctx);
    }
    pub fn func_800E0850(ctx: *zd.Ctx, s: anytype, a: anytype) c_int {
        _ = s;
        _ = a;
        if (arc and ctx.value(u3) == 0) return -(1 << 30) + @as(c_int, ctx.value(u16));
        return small(ctx);
    }
    pub fn func_800E02B4(ctx: *zd.Ctx, s: anytype, a: anytype, b: anytype) c_int {
        _ = s;
        _ = a;
        _ = b;
        return ctx.value(u1);
    }
    pub fn func_800E4660(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype) c_uint {
        _ = a;
        _ = b;
        _ = c;
        return ctx.value(u16);
    }
    pub fn vs_gte_rsqrt(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        const n = rsqrt_calls;
        rsqrt_calls += 1;
        // The distance: the step itself (at least 2: a 0 quotient would trap), or 9-24.
        if (n == 0) return if (ctx.value(u2) == 0 and last_step >= 2) last_step else @as(c_int, ctx.value(u4)) + 9;
        // The denominator: 0 after the + 4096 (the C's `!denom`), 2, a quotient of 1
        // (with a far-below ground the int product wraps: `height < ground`), or 4098-4113.
        return switch (ctx.value(u3)) {
            0 => -4096,
            1 => -4094,
            2 => 30000,
            else => @as(c_int, ctx.value(u4)) + 2,
        };
    }
    pub fn func_800E3600(ctx: *zd.Ctx, s: anytype, a: anytype, b: anytype) void {
        _ = ctx;
        _ = s;
        _ = a;
        _ = b;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const s = ctx.alloc(v.movementStepState);
    const object = ctx.alloc(v.movementCheckObject);
    psx.put(&s.object, object);
    object.flags = if (ctx.chance(8)) 0 else 0x200000;
    kind = ctx.range(usize, 0, 4);
    s.unk0 = if (ctx.chance(4)) 0 else 1;
    s.tile.raw = ctx.int(u32);
    s.tile.p.x = ctx.range(u8, 0, 3);
    s.tile.p.z = ctx.range(u8, 0, 3);
    s.destination.raw = ctx.int(u32);
    s.destination.p.x = ctx.range(u8, 0, 3);
    s.destination.p.z = ctx.range(u8, 0, 3);
    if (kind == 2) {
        s.destination.p.x = s.tile.p.x;
        s.destination.p.z = s.tile.p.z;
    } else if (s.destination.p.x == s.tile.p.x and s.destination.p.z == s.tile.p.z) {
        s.destination.p.x +%= 1;
    }
    s.position.p.x = @as(i16, s.tile.p.x) * 128 + 64 + ctx.range(i16, -14, 14);
    s.position.p.y = ctx.range(i16, -12, 24);
    s.z = @as(i16, s.tile.p.z) * 128 + 64 + ctx.range(i16, -14, 14);
    s.height = ctx.range(i16, 0, 8);
    s.clearance = ctx.range(i16, 0, 4);
    s.angle = ctx.int(i16);
    s.actorId = ctx.range(u8, 0, 16);

    const actor = ctx.alloc(v.movementStepActor);
    actor.y = ctx.range(i16, -12, 12);
    actor.height = ctx.range(u16, 0, 9);
    actor.tile.raw = if (ctx.chance(2)) s.destination.raw else ctx.int(u32);
    v.g.D_800F4538()[s.actorId] = @ptrCast(actor);
    const globals = ctx.alloc(v.D_800F58BC_t);
    v.g.D_800F58BC().* = globals;
    globals.unk14 = if (ctx.chance(3)) 0 else ctx.range(i32, -12, 12);

    const grid = ctx.alloc([4][32]u16);
    for (grid) |*r| {
        for (r) |*t| t.* = ctx.int(u16);
    }
    if (kind == 1) grid[s.destination.p.z][s.destination.p.x] |= 0x40 else if (ctx.chance(2)) grid[s.destination.p.z][s.destination.p.x] &= ~@as(u16, 0x40);
    @as(*u32, @ptrFromInt(0x1F8003C0)).* = psx.addr(grid);
    @as(*c_int, @ptrFromInt(0x1F8003C8)).* = switch (kind) {
        3 => 5,
        4 => 7,
        else => ctx.pick(c_int, &.{ 5, 7, 6 }),
    };
    arc = @as(*c_int, @ptrFromInt(0x1F8003C8)).* == 7;
    // Not below 0: the arc divides by height + ground.
    floor_at_py = @max(0, @as(c_int, s.position.p.y) - s.clearance);
    // A negative x: the C reads it as the low 16 bits, unsigned.
    if (arc and ctx.chance(8)) s.position.p.x = ctx.range(i16, -32768, -1);
    if (arc and globals.unk14 < 0) globals.unk14 = -globals.unk14;
    return .{ s, ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return kind;
}
