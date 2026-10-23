//! zig_diff spec: func_800A8FD4 -> func_800A8FD4.func_800A8FD4 (40564.c).
//! Inputs: an actor whose flags word (0x800 early-out in about one run in six),
//! radius and y-radius (edge values including 0x8000 and 0xFFFF, so a signed
//! read differs), and a position near the origin; a motion vector with a
//! zero component a third of the time; and the 15 slots D_800F4538[2..16],
//! each null, the actor itself, or a fresh object. A fresh object has random
//! skip, distance-flag, class and kind bits, and a box placed on each of the
//! overlap edges (exact and one step past), inside, or far away, so every
//! reject test and the distance test are reached with equal and unequal sides.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A8FD4));

pub const classes = [_][:0]const u8{ "flagged-out", "no-candidate", "candidate-zero-motion", "candidate-moving" };

/// Class of the last prepared input (classify reads it: prepare set it).
var cur_class: usize = 0;

/// The actor's box as the neighbours see it (i32 so bounds never wrap).
const Frame = struct { x: i32, y: i32, z: i32, r: i32, ry: i32, dy: i32 };

fn coord(ctx: *zd.Ctx) i16 {
    return if (ctx.chance(8)) ctx.int(i16) else ctx.range(i16, -400, 400);
}

/// A coordinate around [lo, hi]: inside, on either bound, one step past
/// either bound, or anywhere.
fn place(ctx: *zd.Ctx, lo: i32, hi: i32) i16 {
    const t = ctx.range(u8, 0, 11);
    const x: i32 = switch (t) {
        0...5 => ctx.range(i32, lo, hi),
        6 => lo,
        7 => hi,
        8 => lo - 1,
        9 => hi + 1,
        else => @as(i32, ctx.int(i16)),
    };
    return @intCast(std.math.clamp(x, -32768, 32767));
}

fn inRange(x: i16, lo: i32, hi: i32) bool {
    const w: i32 = x;
    return w >= lo and w <= hi;
}

/// Fills one neighbour; returns whether its box overlaps the actor's
/// (so it reaches the distance test unless a flag rejects it).
fn setupOther(ctx: *zd.Ctx, other: *v.D_800F4538_t, f: Frame) bool {
    var of: u32 = ctx.int(u32);
    of = if (ctx.chance(3)) of | 1 else of & ~@as(u32, 1);
    if (ctx.chance(3)) of = (of & ~@as(u32, 0x0F00_0000)) | 0x0200_0000;
    psx.byteAt(u32, other, 8).* = of;

    var f5: u32 = ctx.int(u32);
    f5 = if (ctx.chance(4)) f5 | 0x0100_0000 else f5 & ~@as(u32, 0x0100_0000);
    psx.byteAt(u32, other, 0x5AC).* = f5;

    other.unk0.unk13 = if (ctx.chance(6)) @as(u8, 0xFC) else ctx.edge(u8, &.{ 0, 0xFB, 0xFD, 0xFF });

    const orad: u16 = ctx.pick(u16, &.{ 0, 1, 40, 200, 0x7FFF, 0x8000, 0xFFFF });
    const oy: u16 = ctx.pick(u16, &.{ 0, 1, 128, 0x8000, 0xFFFF });
    std.mem.writeInt(u16, other.unk640[0..2], orad, .little);
    std.mem.writeInt(u16, other.unk640[2..4], oy, .little);

    const ro: i32 = orad;
    const yo: i32 = oy;
    const xlo = f.x - f.r - ro;
    const xhi = f.x + f.r + ro;
    const zlo = f.z - f.r - ro;
    const zhi = f.z + f.r + ro;
    const ylo = f.y - f.ry - f.dy;
    const yhi = f.y + yo;
    const bx = place(ctx, xlo, xhi);
    const bz = place(ctx, zlo, zhi);
    const by = place(ctx, ylo, yhi);
    other.unk0.position.vx = bx;
    other.unk0.position.vy = by;
    other.unk0.position.vz = bz;
    return inRange(bx, xlo, xhi) and inRange(bz, zlo, zhi) and inRange(by, ylo, yhi);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    const motion = ctx.alloc(v.SVECTOR);
    const table = v.g.D_800F4538();

    const early = ctx.chance(6);
    var flags: u32 = ctx.int(u32);
    flags = if (early) flags | 0x800 else flags & ~@as(u32, 0x800);
    psx.byteAt(u32, actor, 0x5AC).* = flags;
    const dy: i32 = if ((flags & 8) != 0) 128 else 0;

    const radius: u16 = ctx.pick(u16, &.{ 0, 1, 40, 200, 0x7FFF, 0x8000, 0xFFFF });
    const aY: u16 = ctx.pick(u16, &.{ 0, 1, 128, 0x8000, 0xFFFF });
    std.mem.writeInt(u16, actor.unk640[0..2], radius, .little);
    std.mem.writeInt(u16, actor.unk640[2..4], aY, .little);

    const ax = coord(ctx);
    const ay = coord(ctx);
    const az = coord(ctx);
    actor.unk0.position.vx = ax;
    actor.unk0.position.vy = ay;
    actor.unk0.position.vz = az;
    actor.unk0.position.pad = 0;

    motion.vx = if (ctx.chance(3)) 0 else ctx.pick(i16, &.{ 1, -1, 300, 32767, -32768 });
    motion.vz = if (ctx.chance(3)) 0 else ctx.pick(i16, &.{ 1, -1, 300, 32767, -32768 });
    motion.vy = ctx.int(i16);
    motion.pad = ctx.int(i16);

    const frame = Frame{ .x = ax, .y = ay, .z = az, .r = radius, .ry = aY, .dy = dy };

    var any_inside = false;
    var slot: usize = 2;
    while (slot < 17) : (slot += 1) {
        if (ctx.chance(6)) {
            table[slot] = null;
        } else if (ctx.chance(12)) {
            table[slot] = actor;
        } else {
            const other = ctx.alloc(v.D_800F4538_t);
            table[slot] = other;
            if (setupOther(ctx, other, frame)) any_inside = true;
        }
    }

    cur_class = if (early)
        0
    else if (!any_inside)
        1
    else if (motion.vx == 0 and motion.vz == 0)
        2
    else
        3;

    return .{ actor, motion };
}

pub fn classify(args: Args) usize {
    _ = args;
    return cur_class;
}
