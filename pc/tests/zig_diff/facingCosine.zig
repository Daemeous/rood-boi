//! zig_diff spec: func_800E437C -> pc.facing_cosine.facingCosine (D2: pc/src/battle_gte.c, through its static facing_trig).
//! Inputs: two XYZ short positions (edges or any i16, so the X and Z differences
//! leave the i16 range), an angle (the quarter turns, whose folded lookups are
//! the table's shortcut, their neighbours, or any int) and a distance (zero a
//! sixth of the time). The table _trig_table is random (the harness fills every
//! global), so each lookup returns a distinct value. VectorNormal's stub makes
//! the unit vector from the direction it is handed (so a wrong direction
//! shows): in half the runs small values (the clamp edges come up), in the rest
//! any int (the unsigned products wrap); the stack vectors are tagged in the
//! call log, so the stub is what carries them into the result.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_gte");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").facingCosine));

pub const classes = [_][:0]const u8{ "distance-zero", "quarter-turn", "table" };

var small: bool = false;
/// The angle this iteration (the stub recomputes the weights from it) and, when
/// set, the result the stub steers the unit vector to: the clamp edges
/// (-0x1001, -0x1000, 0xFFF, 0x1000, 0x1001 and their neighbours) are exact
/// products of random table weights, so random unit vectors never land on them.
var angle_now: c_int = 0;
var steer: ?c_int = null;
const steer_targets = [_]c_int{ -0x1001, -0x1000, -0xFFF, 0xFFF, 0x1000, 0x1001, -0x1002, 0x1002 };

fn trig(angle_in: c_uint) c_int {
    var angle = angle_in;
    var value: c_int = undefined;
    if (angle & 0x400 != 0 and angle & 0x3ff == 0) {
        value = 0x1000;
    } else {
        var offset: usize = 0;
        if (angle & 0x400 != 0) angle = 0x800 -% angle;
        var index: usize = angle & 0x1ff;
        if (angle & 0x200 != 0) {
            index = 0x200 - index;
            offset = 2;
        }
        value = @as(*align(1) const u16, @ptrCast(v.g._trig_table() + offset + index * 4)).*;
    }
    return if (angle & 0x800 != 0) -%value else value;
}

fn steered(d: [*]const c_int, u: [*]c_int) bool {
    const target = steer orelse return false;
    const base: c_uint = @bitCast((0xc00 -% angle_now) & 0xfff);
    const zw: i64 = trig(base);
    const xw: i64 = trig(base +% 0x400);
    if (xw == 0) return false;
    _ = d;
    var uz: i64 = 0;
    while (uz < 256) : (uz += 1) {
        // ux * xw + uz * zw in [target << 12, target << 12 + 4095], in 32-bit wrapping arithmetic.
        const want = @as(i64, target) * 4096 - uz * zw;
        const ux = @divTrunc(want + (if (xw > 0) xw - 1 else xw + 1), xw);
        if (ux < -0x80000000 or ux > 0x7FFFFFFF) continue;
        const sum: c_uint = @as(c_uint, @bitCast(@as(c_int, @intCast(ux)))) *% @as(c_uint, @bitCast(@as(c_int, @intCast(xw)))) +%
            @as(c_uint, @intCast(uz)) *% @as(c_uint, @bitCast(@as(c_int, @intCast(zw))));
        if (@as(c_int, @bitCast(sum)) >> 12 == target) {
            u[0] = @intCast(ux);
            u[2] = @intCast(uz);
            return true;
        }
    }
    return false;
}

const edges = [_]i16{ 0, 1, -1, 0x7FFF, -0x8000, 100, -100, 0x4000 };

fn position(ctx: *zd.Ctx) [*]const i16 {
    const p: [*]i16 = @ptrCast(@alignCast(ctx.allocBytes(8, 16)));
    for (0..4) |i| p[i] = ctx.pick(i16, &edges);
    return p;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    small = ctx.chance(2);
    steer = if (ctx.chance(3)) ctx.edge(c_int, &steer_targets) else null;
    const from = position(ctx);
    const to = position(ctx);
    const angle = switch (ctx.range(u8, 0, 3)) {
        0 => ctx.edge(c_int, &.{ 0, 0x400, 0x800, 0xC00, 0x1000, -0x400, 0x3FF, 0x401, 0x7FF, 0x801, 0xBFF, 0xC01 }),
        1 => ctx.edge(c_int, &.{ 0, 0x400, 0x800, 0xC00 }) + ctx.range(c_int, -2, 2),
        2 => ctx.range(c_int, -0x2000, 0x2000),
        else => ctx.int(c_int),
    };
    const distance = if (ctx.chance(6)) 0 else ctx.pick(c_uint, &.{ 1, 0x1000, 0xFFFFFFFF });
    angle_now = angle;
    return .{ from, to, angle, distance };
}

fn shortcut(angle: c_uint) bool {
    return angle & 0x400 != 0 and angle & 0x3ff == 0;
}

pub fn classify(args: Args) usize {
    if (args[3] == 0) return 0;
    const base: c_uint = @bitCast((0xc00 -% args[2]) & 0xfff);
    return if (shortcut(base) or shortcut(base +% 0x400)) 1 else 2;
}

pub const stubs = struct {
    pub fn VectorNormal(ctx: *zd.Ctx, v0: anytype, v1: anytype) c_int {
        const d: [*]const c_int = @ptrCast(@alignCast(v0.?));
        const u: [*]c_int = @ptrCast(@alignCast(v1.?));
        // Y and the pad are written 0 by the C: they take part, so a wrong one shows.
        const salt: i64 = @as(i64, d[1]) * 7 + d[3];
        if (steered(d, u)) {
            // the result is steered to a clamp edge
        } else if (small) {
            u[0] = @as(c_int, @intCast(@mod(@as(i64, d[0]) * 37 + salt, 0x2001))) - 0x1000;
            u[2] = @as(c_int, @intCast(@mod(@as(i64, d[2]) * 53 + salt, 0x2001))) - 0x1000;
        } else {
            u[0] = d[0] *% 0x10003 +% @as(c_int, @truncate(salt));
            u[2] = d[2] *% 0x20005 +% @as(c_int, @truncate(salt));
        }
        u[1] = ctx.value(c_int);
        u[3] = ctx.value(c_int);
        return ctx.value(c_int);
    }
};
