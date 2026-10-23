//! zig_diff spec: func_800A8E84 -> func_800A8E84.func_800A8E84 (40564.c).
//! Inputs: an actor with unkF zero (or not, the early return), with the
//! 0x800 and 0x1000 flags of 0x5AC set sometimes (early returns). The table
//! D_800F4538[2..16] is always filled with valid objects or nulls, whatever
//! the early-return class, so a port that goes on into the loop reads
//! defined memory. Per entry: null, the actor itself, or an object that
//! fails one gate at random (flags bit 0, 0x1000000 in 0x5AC, unk13 == 0xFC,
//! the 0xF000000 pattern, the scratchpad exclusion byte) or fails the box
//! test by its random position. Most iterations also place one candidate
//! object on the position's box boundary: each axis either sits on an edge
//! (so the comparison sees equal sides) or falls just outside it; the
//! actor's vy is placed on or past the candidate's vy edge. Radii are 0, 1,
//! 2, 40 or random u16 (so the signed read differs).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A8E84));

pub const classes = [_][:0]const u8{
    "unkF-set",
    "flag-0x800",
    "flag-0x1000",
    "candidate-hit",
    "candidate-blocked",
    "no-candidate",
};

var class_of: usize = 0;

fn wordAt(p: anytype, off: isize) *align(1) u32 {
    return psx.byteAt(u32, p, off);
}

fn halfAt(p: anytype, off: isize) *align(1) u16 {
    return psx.byteAt(u16, p, off);
}

/// The scratchpad exclusion byte the C reads for table index i:
/// ((actorCollisionExclusion*)(0x1F8003BC + i))->excluded.
fn excludedAt(i: usize) *u8 {
    return @ptrFromInt(zd.scratchpad + 0x3C0 + i);
}

/// mode 0: base + r, 1: base - r, 2: base + r + 1, 3: base - r - 1 (wrapped to i16).
fn axis(base: i16, r: i32, mode: u8) i16 {
    const off: i32 = switch (mode) {
        0 => r,
        1 => -r,
        2 => r + 1,
        else => -r - 1,
    };
    return @truncate(@as(i32, base) + off);
}

fn fillOther(ctx: *zd.Ctx, o: *v.D_800F4538_t, i: usize) void {
    o.unk0.position.vx = ctx.int(i16);
    o.unk0.position.vy = ctx.int(i16);
    o.unk0.position.vz = ctx.int(i16);
    o.unk0.unk13 = if (ctx.chance(8)) 0xFC else ctx.int(u8);
    var fl: u32 = ctx.int(u32);
    fl = if (ctx.chance(6)) fl | 1 else fl & ~@as(u32, 1);
    fl = if (ctx.chance(8)) (fl & ~@as(u32, 0x0F00_0000)) | 0x0200_0000 else fl;
    wordAt(o, 8).* = fl;
    var w: u32 = ctx.int(u32);
    w = if (ctx.chance(6)) w | 0x0100_0000 else w & ~@as(u32, 0x0100_0000);
    wordAt(o, 0x5AC).* = w;
    halfAt(o, 0x640).* = ctx.pick(u16, &.{ 0, 1, 40 });
    halfAt(o, 0x642).* = ctx.pick(u16, &.{ 0, 1, 40 });
    excludedAt(i).* = if (ctx.chance(3)) 1 else 0;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    const pos = ctx.alloc(v.SVECTOR);
    const table = v.g.D_800F4538();

    // The actor's radius-like half (unk640[2]) first: the candidate's
    // geometry is built from it.
    halfAt(actor, 0x642).* = ctx.pick(u16, &.{ 0, 1, 2, 40 });
    const ar: i32 = halfAt(actor, 0x642).*;

    // The table is always populated, so every class (early returns included)
    // leaves the loop's reads on valid memory.
    const cand: ?usize = if (ctx.chance(5)) ctx.range(usize, 2, 16) else null;

    var i: usize = 2;
    while (i < 17) : (i += 1) {
        if (cand != null and cand.? == i) continue;
        const roll = ctx.range(u8, 0, 7);
        if (roll == 0) {
            table[i] = null;
            continue;
        }
        if (roll == 1) {
            table[i] = actor;
            excludedAt(i).* = ctx.int(u8);
            continue;
        }
        const o = ctx.alloc(v.D_800F4538_t);
        fillOther(ctx, o, i);
        table[i] = o;
    }

    var outcome: usize = 5;
    if (cand) |t| {
        const o = ctx.alloc(v.D_800F4538_t);
        fillOther(ctx, o, t);
        // Clean gates: bit 0, the 0xF000000 pattern and 0x1000000 all clear.
        wordAt(o, 8).* &= ~@as(u32, 0x0F00_0001);
        wordAt(o, 0x5AC).* &= ~@as(u32, 0x0100_0000);
        o.unk0.unk13 = if (ctx.chance(6)) 0xFC else 0;

        const r: i32 = halfAt(o, 0x640).*;
        const tr: i32 = halfAt(o, 0x642).*;
        const xm = ctx.range(u8, 0, 3);
        const zm = ctx.range(u8, 0, 3);
        const vm = ctx.range(u8, 0, 3);
        pos.vx = axis(o.unk0.position.vx, r, xm);
        pos.vz = axis(o.unk0.position.vz, r, zm);
        // vy: 0 = on the actor-side edge, 1 = on the other-side edge,
        // 2 = just past the first, 3 = just past the second.
        actor.unk0.position.vy = switch (vm) {
            0 => axis(o.unk0.position.vy, ar, 0),
            1 => axis(o.unk0.position.vy, tr, 1),
            2 => axis(o.unk0.position.vy, ar, 2),
            else => axis(o.unk0.position.vy, tr, 3),
        };

        excludedAt(t).* = if (ctx.chance(4)) 1 else 0;
        table[t] = o;
        const passes = excludedAt(t).* == 0 and o.unk0.unk13 != 0xFC and xm < 2 and zm < 2 and vm < 2;
        outcome = if (passes) 3 else 4;
    } else {
        pos.vx = ctx.int(i16);
        pos.vz = ctx.int(i16);
        actor.unk0.position.vy = ctx.int(i16);
    }
    pos.vy = ctx.int(i16);

    // Flags and unkF last: they pick the class, and the table above is
    // already valid whichever early return the C takes.
    actor.unk0.unkF = if (ctx.chance(6)) 1 + ctx.range(u8, 0, 254) else 0;
    const sel = ctx.range(u8, 0, 9);
    var af: u32 = ctx.int(u32) & ~@as(u32, 0x1800);
    if (sel == 0) af |= 0x800;
    if (sel == 1) af |= 0x1000;
    wordAt(actor, 0x5AC).* = af;

    if (actor.unk0.unkF != 0) {
        class_of = 0;
    } else if (sel == 0) {
        class_of = 1;
    } else if (sel == 1) {
        class_of = 2;
    } else {
        class_of = outcome;
    }
    return .{ actor, pos };
}

pub fn classify(args: Args) usize {
    _ = args;
    return class_of;
}
