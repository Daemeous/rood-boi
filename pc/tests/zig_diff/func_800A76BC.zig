//! zig_diff spec: func_800A76BC -> func_800A76BC.func_800A76BC (3A1A0.c).
//! Inputs: an actor with a radius 0-128, its held-direction (0x4000), crouch
//! (0x5AC bit 3) and 0x800000 / 0x70000 flags both ways, a facing 0-7; a
//! move with either axis 0, equal, or one larger, of either sign; the
//! probe-only mode. The tile probe stub (func_800A8B34, which writes the
//! probe's height and area as the real one does) returns a result around
//! every threshold the C tests (0-5, 0xFF, the -0x40 / -0x100 floors) and a
//! height around the actor's, so the climb steps are 1-3 and the two side
//! heights tie sometimes; it also moves D_800F49F0, which the C reads after
//! it. The tile lookup returns null or a tile, the move check 0 or not.
//! In the probe-only mode the actor's tile flags are set only with a probe
//! that finds no floor: with a floor the C would store an uninitialised local
//! into D_800F49F0 (the port stores null).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_3A1A0");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A76BC));

pub const classes = [_][:0]const u8{ "probe-only", "x-only", "z-only", "diagonal" };

var base_vy: i16 = 0;
/// Probe-only with the tile flags set: the probe reports no floor (0xFF), so
/// the C returns before it would store its uninitialised tile pointer.
var force_ff = false;
var tiles: *[4]u32 = undefined;

pub const stubs = struct {
    pub fn func_800A8B34(ctx: *zd.Ctx, p: anytype, d: anytype) c_int {
        _ = d;
        const s: *v.SVECTOR = @ptrCast(@alignCast(p));
        s.vy = base_vy +% @as(i16, ctx.value(u3)) * 32 -% 96;
        s.pad = ctx.value(i16);
        if (ctx.value(u2) == 0) v.g.D_800F49F0().* = @ptrCast(&tiles[ctx.value(u2)]);
        if (force_ff) return 0xFF;
        return switch (ctx.value(u4)) {
            0, 1, 2 => 0,
            3, 4 => 1,
            5 => 2,
            6 => 3,
            7 => 4,
            8 => 5,
            9 => 0xFF,
            10 => -0x40,
            11 => -0x41,
            12 => -0x100,
            13 => -0x101,
            14 => -0x3F,
            else => ctx.value(c_int),
        };
    }
    pub fn func_800A8D64(ctx: *zd.Ctx, p: anytype, a: anytype) [*c]c_uint {
        _ = p;
        _ = a;
        return if (ctx.value(u2) == 0) null else &tiles[ctx.value(u2)];
    }
    pub fn func_800A8FD4(ctx: *zd.Ctx, a: anytype, m: anytype) c_int {
        _ = a;
        _ = m;
        return if (ctx.value(u3) == 0) 0 else 1;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    tiles = ctx.alloc([4]u32);
    const a = ctx.alloc(v.D_800F4538_t);
    a.unk63C = ctx.pick(u16, &.{ 0, 64, 100, 128 });
    a.unk0.position.vx = ctx.range(i16, 0, 4000);
    a.unk0.position.vz = ctx.range(i16, 0, 4000);
    a.unk0.position.vy = ctx.range(i16, -500, 500);
    base_vy = a.unk0.position.vy;
    a.unk17FE = ctx.range(u8, 0, 7);
    var w8: u32 = ctx.int(u32);
    if (ctx.chance(2)) w8 &= ~@as(u32, 0x70000);
    if (ctx.chance(2)) w8 &= ~@as(u32, 0x800000);
    psx.byteAt(u32, a, 8).* = w8;
    var w5: u32 = ctx.int(u32);
    if (ctx.chance(2)) w5 &= ~@as(u32, 8);
    if (ctx.chance(3)) w5 &= ~@as(u32, 0x4000);
    psx.byteAt(u32, a, 0x5AC).* = w5;
    if (ctx.chance(2)) psx.byteAt(u32, a, 0xC).* &= ~@as(u32, 0xF);

    const m = ctx.alloc(v.SVECTOR);
    const big = ctx.edge(i16, &.{ 1, 7, 16, 40, 100, 3000 });
    const sx: i16 = if (ctx.chance(2)) 1 else -1;
    const sz: i16 = if (ctx.chance(2)) 1 else -1;
    switch (ctx.range(u8, 0, 4)) {
        0 => {
            m.vx = 0;
            m.vz = sz * big;
        },
        1 => {
            m.vx = sx * big;
            m.vz = 0;
        },
        2 => {
            m.vx = sx * big;
            m.vz = sz * big;
        },
        3 => {
            m.vx = sx * big;
            m.vz = sz * @divTrunc(big, 2);
        },
        else => {
            m.vx = sx * @divTrunc(big, 2);
            m.vz = sz * big;
        },
    }
    if (ctx.chance(6)) {
        m.vx = 0;
        m.vz = 0;
        if (ctx.chance(2)) psx.byteAt(u32, a, 8).* &= ~@as(u32, 0x70000);
    }
    const result = ctx.alloc(c_int);
    const probeOnly: c_int = if (ctx.chance(5)) 1 else 0;
    force_ff = false;
    if (probeOnly != 0) {
        if (ctx.chance(4)) {
            psx.byteAt(u32, a, 0xC).* |= 1;
            force_ff = true;
        } else psx.byteAt(u32, a, 0xC).* &= ~@as(u32, 0xF);
    }
    return .{ a, m, result, probeOnly };
}

pub fn classify(args: Args) usize {
    if (args[3] != 0) return 0;
    const m = args[1].?;
    if (m.vz == 0) return 1;
    if (m.vx == 0) return 2;
    return 3;
}
