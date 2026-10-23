//! zig_diff spec: func_800A3760 -> func_800A3760.func_800A3760 (3A1A0.c).
//! Inputs: a table of 17 actor pointers (some null, some flagged skip), an
//! index arg0 in 0..16 (self, or a null self), a radius arg1 and a vertical
//! limit arg2, and actor positions in four modes. Mode 0 is a null self (the
//! early return). Mode 1 (crowded) uses small offsets, so the distance test,
//! the equal-distance tie break, the vertical limit's edges and the equal
//! radius are all hit. Mode 2 (sparse) is mostly out of range. Mode 3
//! (extreme) uses full i16 positions, so dx*dx and dz*dz overflow int and go
//! negative, with arg1 and arg2 near the int range. func_8009E180 is a pure
//! angle read of the two positions: the stub changes nothing and returns the
//! +-1024 edges and values just outside them.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A3760));

pub const classes = [_][:0]const u8{ "self-null", "crowded", "sparse", "extreme" };

var mode: usize = 0;

pub const stubs = struct {
    /// Reads the two positions and changes nothing. Returns the angle
    /// edges (-1024, 1024), values just outside them, and a spread of
    /// in-range and out-of-range angles.
    pub fn func_8009E180(ctx: *zd.Ctx, self: anytype, pos: anytype) c_int {
        _ = self;
        _ = pos;
        const k = ctx.value(u8);
        if (k < 24) return -1024;
        if (k < 48) return 1024;
        if (k < 56) return -1025;
        if (k < 64) return 1025;
        return @divTrunc(@as(c_int, ctx.value(i16)), 16);
    }
};

fn placeSelf(ctx: *zd.Ctx, o: *v.D_800F4538_t, m: usize) void {
    psx.setBits(&o.unk0, "skip", @intFromBool(ctx.chance(2)));
    if (m == 3) {
        o.unk0.position.vx = ctx.int(i16);
        o.unk0.position.vy = ctx.int(i16);
        o.unk0.position.vz = ctx.int(i16);
    } else {
        o.unk0.position.vx = ctx.range(i16, -400, 400);
        o.unk0.position.vy = ctx.range(i16, -400, 400);
        o.unk0.position.vz = ctx.range(i16, -400, 400);
    }
}

fn placeActor(ctx: *zd.Ctx, o: *v.D_800F4538_t, s: *v.D_800F4538_t, m: usize, arg1: c_int, arg2: c_int) void {
    psx.setBits(&o.unk0, "skip", @intFromBool(ctx.chance(5)));
    if (m == 3) {
        o.unk0.position.vx = ctx.int(i16);
        o.unk0.position.vy = ctx.int(i16);
        o.unk0.position.vz = ctx.int(i16);
        return;
    }
    var dx: c_int = undefined;
    var dy: c_int = undefined;
    var dz: c_int = undefined;
    if (m == 1) {
        dx = if (ctx.chance(2)) ctx.range(c_int, -2, 2) else ctx.range(c_int, -300, 300);
        dz = if (ctx.chance(2)) ctx.range(c_int, -2, 2) else ctx.range(c_int, -300, 300);
        dy = switch (ctx.range(u8, 0, 3)) {
            0 => arg2,
            1 => -arg2,
            2 => arg2 + 1,
            else => ctx.range(c_int, -300, 300),
        };
        if (ctx.chance(4)) {
            dx = arg1;
            dz = 0;
        }
    } else {
        dx = ctx.range(c_int, -2000, 2000);
        dz = ctx.range(c_int, -2000, 2000);
        dy = if (ctx.chance(3)) arg2 else ctx.range(c_int, -2000, 2000);
    }
    o.unk0.position.vx = s.unk0.position.vx +% @as(i16, @truncate(dx));
    o.unk0.position.vy = s.unk0.position.vy +% @as(i16, @truncate(dy));
    o.unk0.position.vz = s.unk0.position.vz +% @as(i16, @truncate(dz));
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F4538();
    var objs: [17]*v.D_800F4538_t = undefined;
    for (&objs) |*o| o.* = ctx.alloc(v.D_800F4538_t);

    const arg0: c_int = ctx.range(c_int, 0, 16);
    const si: usize = @intCast(arg0);
    mode = switch (ctx.range(u8, 0, 99)) {
        0...5 => 0,
        6...49 => 1,
        50...79 => 2,
        else => 3,
    };
    const big = mode == 3;
    const arg1: c_int = if (big) ctx.range(c_int, -46000, 46000) else ctx.range(c_int, -300, 300);
    const arg2: c_int = if (big) ctx.range(c_int, -2000000000, 2000000000) else ctx.range(c_int, -3, 400);

    const self = objs[si];
    placeSelf(ctx, self, mode);
    for (objs, 0..) |o, i| {
        if (i != si) placeActor(ctx, o, self, mode, arg1, arg2);
    }

    for (objs, 0..) |o, i| {
        const dead = if (i == si) mode == 0 else ctx.chance(10);
        table[i] = if (dead) null else o;
    }

    return .{ arg0, arg1, arg2 };
}

pub fn classify(args: Args) usize {
    _ = args;
    return mode;
}
