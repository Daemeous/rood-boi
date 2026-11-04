//! zig_diff spec: func_800E6C34 -> trace_attack.traceAttack (D2: pc/src/battle_movement.c, with its static clear_of_obstacles).
//! Inputs: a start and an end point packed as the C reads them (x in the low
//! half and y in the high half of word 0, z in the low half of word 1, the
//! high half of word 1 random), small coordinates (z mostly positive: an
//! actor's z is read unsigned), the end sometimes equal to the start (a zero
//! step: `ahead` is zero) or on the same height (so the floor test can meet
//! the path height); an output pair; `self` -1..16.
//! The 16 actor slots (vs_battle_actors, D_800F45E0, D_800F4538): empty,
//! hidden by the flag byte 0x1C (bits of 0xEA, the bits outside it), engaged
//! (D_800F45E0: bytes 0x12 and 0x1A) or a model (pose 1, 2 and other, byte
//! 0x5AF bit 0, reach 0x63C and height 0x63E); the body position is a step
//! of the path plus an offset that is 0, one less than, at or above the reach
//! and height, so every comparison is met at its edge, in front of and behind
//! the start. The callees are stubs: func_8008D2C0 fills 0-8 obstacle points
//! near the path, func_8008DC7C returns floors at, below and above the path
//! height (an edge of 15-bit sign too).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_movement");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").traceAttack));

pub const classes = [_][:0]const u8{ "no-actors", "engaged-actor", "model-actor" };

var origin = [3]c_int{ 0, 0, 0 };
var step = [3]c_int{ 0, 0, 0 };
var flat = false;
// A clear path (a fifth of the inputs): no actors, no obstacle points, a floor
// far above the path, so the trace runs all 32 steps and returns 0 (the
// step-count comparison meets its limit).
var clear = false;

fn word(p: [*]u8, off: usize) *align(1) u32 {
    return @ptrCast(p + off);
}

fn half(p: [*]u8, off: usize) *align(1) u16 {
    return @ptrCast(p + off);
}

pub const stubs = struct {
    pub fn func_8008D2C0(ctx: *zd.Ctx, points: anytype) c_int {
        const raw: ?*anyopaque = points;
        const out: [*]u32 = @ptrCast(@alignCast(raw.?));
        const count: usize = if (clear or ctx.value(u8) < 100) 0 else ctx.value(u8) % 9;
        var i: usize = 0;
        while (i < count) : (i += 1) {
            const k: c_int = ctx.value(u8) % 33;
            const x = origin[0] + k * step[0] + (@as(c_int, ctx.value(u8) % 140) - 70);
            const y = origin[1] + k * step[1] + (@as(c_int, ctx.value(u8) % 60) - 30);
            const z = origin[2] + k * step[2] + (@as(c_int, ctx.value(u8) % 140) - 70);
            out[i * 2] = (@as(u32, @bitCast(x)) & 0xffff) | (@as(u32, @bitCast(y)) << 16);
            out[i * 2 + 1] = (@as(u32, @bitCast(z)) & 0xffff) | (@as(u32, ctx.value(u16)) << 16);
        }
        return @intCast(count);
    }

    pub fn func_8008DC7C(ctx: *zd.Ctx, x: anytype, z: anytype) c_short {
        _ = x;
        _ = z;
        const r = ctx.value(u8);
        if (clear) return 16000;
        if (flat) return @intCast(origin[1] + @as(c_int, ctx.edge(i8, &.{ -1, 0, 1, 1, 1, 100 })));
        if (r < 24) return ctx.value(i16);
        if (r < 120) return @intCast(origin[1] + 2000);
        return @intCast(origin[1] + @as(c_int, ctx.value(i8)) * 8);
    }
};

fn pack(low: c_int, high: c_int) u32 {
    return (@as(u32, @bitCast(low)) & 0xffff) | (@as(u32, @bitCast(high)) << 16);
}

/// A body (engaged object or model) whose position is a step `t` of the path
/// plus the offsets; `height` is the body's own height before halving.
fn place(ctx: *zd.Ctx, body: [*]u8, reach: c_int, height: c_int) void {
    const t = ctx.range(c_int, -4, 36);
    const half_height = height >> 1;
    const edges = [_]c_int{ 0, 0, reach - 1, reach, reach + 1, -(reach - 1), -reach, 5 };
    const edges_y = [_]c_int{ 0, 0, half_height - 1, half_height, half_height + 1, -(half_height - 1), -half_height, 3 };
    const off_x = if (ctx.chance(3)) ctx.edge(c_int, &edges) else ctx.range(c_int, -reach - 8, reach + 8);
    const off_y = if (ctx.chance(3)) ctx.edge(c_int, &edges_y) else ctx.range(c_int, -half_height - 8, half_height + 8);
    const off_z = if (ctx.chance(3)) ctx.edge(c_int, &edges) else ctx.range(c_int, -reach - 8, reach + 8);
    const px = origin[0] + (t + 1) * step[0] + off_x;
    const py = origin[1] + (t + 1) * step[1] + off_y + half_height;
    const pz = origin[2] + (t + 1) * step[2] + off_z;
    word(body, 0x1c).* = pack(px, py);
    half(body, 0x20).* = @bitCast(@as(i16, @intCast(pz)));
}

var mode: usize = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    const span: i16 = if (ctx.chance(4)) 3000 else 300;
    const from = ctx.allocBytes(8, 16);
    const to = ctx.allocBytes(8, 16);
    const out = ctx.allocBytes(8, 16);
    for (out[0..8]) |*byte| byte.* = ctx.int(u8);
    const fx: c_int = ctx.range(i16, -span, span);
    const fy: c_int = ctx.range(i16, -span, span);
    const fz: c_int = ctx.range(i16, -60, span);
    var tx: c_int = ctx.range(i16, -span, span);
    var ty: c_int = ctx.range(i16, -span, span);
    var tz: c_int = ctx.range(i16, -60, span);
    if (ctx.chance(8)) {
        tx = fx;
        ty = fy;
        tz = fz;
    }
    flat = ctx.chance(4);
    if (flat) ty = fy;
    word(from, 0).* = pack(fx, fy);
    word(from, 4).* = pack(fz, ctx.int(i16));
    word(to, 0).* = pack(tx, ty);
    word(to, 4).* = pack(tz, ctx.int(i16));
    origin = .{ fx, fy, fz };
    step = .{ (tx - fx) >> 5, (ty - fy) >> 5, (tz - fz) >> 5 };

    clear = ctx.chance(5);
    const empty = clear or ctx.chance(6);
    var engaged_any = false;
    var model_any = false;
    for (0..16) |i| {
        v.g.vs_battle_actors()[i] = null;
        v.g.D_800F45E0()[i] = null;
        v.g.D_800F4538()[i] = null;
        if (empty or ctx.chance(4)) continue;
        const actor = ctx.allocBytes(0x40, 16);
        for (actor[0..0x40]) |*byte| byte.* = ctx.int(u8);
        actor[0x1c] = if (ctx.chance(4)) ctx.edge(u8, &.{ 0x02, 0x08, 0x20, 0x80, 0xea, 0xff, 0x15 }) else if (ctx.chance(2)) 0x15 else 0;
        v.g.vs_battle_actors()[i] = @ptrCast(actor);
        if (ctx.chance(3)) {
            const body = ctx.allocBytes(0x40, 16);
            for (body[0..0x40]) |*byte| byte.* = ctx.int(u8);
            body[0x12] = if (ctx.chance(5)) 0 else ctx.edge(u8, &.{ 1, 0x80, 0xff });
            body[0x1a] = if (ctx.chance(5)) ctx.edge(u8, &.{ 1, 0x80 }) else 0;
            place(ctx, body, 0x3f, 0x80);
            v.g.D_800F45E0()[i] = @ptrCast(body);
            if (body[0x12] != 0 and body[0x1a] == 0 and actor[0x1c] & 0xea == 0) engaged_any = true;
        } else if (!ctx.chance(5)) {
            const model = ctx.allocBytes(0x700, 16);
            for (model[0..0x700]) |*byte| byte.* = ctx.int(u8);
            model[0xb] = (model[0xb] & 0xf0) | ctx.edge(u8, &.{ 0, 1, 2, 3, 4, 0xf });
            model[0x5af] = ctx.edge(u8, &.{ 0, 0, 1, 2, 3, 0x80 });
            const reach: c_int = ctx.edge(c_int, &.{ 0x3f, 0x20, 0x40, 0x80, 0x100, 1 });
            const height: c_int = ctx.edge(c_int, &.{ 0x80, 0x40, 0x41, 0x20, 0x200, 1 });
            half(model, 0x63c).* = @intCast(reach);
            half(model, 0x63e).* = @intCast(height);
            place(ctx, model, reach, height);
            v.g.D_800F4538()[i] = @ptrCast(model);
            if (actor[0x1c] & 0xea == 0) model_any = true;
        }
    }
    mode = if (engaged_any) 1 else if (model_any) 2 else 0;
    return .{ @ptrCast(@alignCast(from)), @ptrCast(@alignCast(to)), @ptrCast(@alignCast(out)), ctx.range(c_int, -1, 16) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return mode;
}
