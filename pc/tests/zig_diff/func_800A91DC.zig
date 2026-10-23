//! zig_diff spec: func_800A91DC -> func_800A91DC.func_800A91DC (40564.c).
//! Inputs: three arguments drawn from edge sets that the slots' fields are
//! built to match (arg0/arg1 against unk5C/unk5E, arg2 zero and nonzero with
//! unk1A zero and nonzero). Each of the 16 slots of D_800F45E0 is either null
//! or an arena object with its unk8_0 flag, unk12, unk6C[8].unk3, unk1E and
//! the match fields set so that a winner is usually found. unk1E is biased to
//! 4096 (the C's starting var_t2) and to the signed extremes, so the
//! `var_t2 >= unk1E` test sees equal sides often and negative values both ways.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A91DC));

pub const classes = [_][:0]const u8{ "no-match", "match", "tied-match", "match-at-0" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0: c_int = ctx.edge(c_int, &.{ 0, 1, 2, 0x7F, 0xFF, 0x100, -1 });
    const arg1: c_int = ctx.edge(c_int, &.{ 0, 1, 0x7F, 0xFF, 0x100, -1 });
    const arg2: c_int = ctx.edge(c_int, &.{ 0, 0, 1, -1, 0x7FFF_FFFF, -0x8000_0000 });

    // Every slot is read by the C: set all 16 (null or a valid arena object).
    const slots = v.g.D_800F45E0();
    for (0..16) |i| {
        if (ctx.chance(8)) {
            slots[i] = null;
            continue;
        }
        const o = ctx.alloc(v.D_800F45E0_t);
        slots[i] = o;
        o.unk12 = ctx.edge(u8, &.{ 0xFF, 0xFF, 0xFF, 0, 0xFE });
        o.unk6C[8].unk3 = if (ctx.chance(5)) ctx.int(u8) else o.unk12;
        // Low byte of the argument (0..255): a match is possible for in-range values.
        o.unk5C = if (ctx.chance(4)) ctx.int(u8) else @as(u8, @intCast(arg0 & 0xFF));
        o.unk5E = if (ctx.chance(4)) ctx.int(u8) else @as(u8, @intCast(arg1 & 0xFF));
        o.unk1A = ctx.edge(i16, &.{ 0, 0, 1, -1 });
        o.unk1E = ctx.edge(i16, &.{ 4096, 4096, 4096, 4095, 4097, 0, -1, 100, 32767, -32768 });
        psx.setBits(o, "unk8_0", @intFromBool(ctx.chance(5)));
    }

    return .{ arg0, arg1, arg2 };
}

pub fn classify(args: Args) usize {
    const a0 = args[0];
    const a1 = args[1];
    const a2 = args[2];
    const slots = v.g.D_800F45E0();
    var best: c_int = 4096;
    var winner: usize = 0;
    var found = false;
    var tied = false;
    for (slots, 0..) |maybe, i| {
        const o = maybe orelse continue;
        if (psx.getBits(o, "unk8_0") != 0) continue;
        if (a2 != 0 and o.unk1A != 0) continue;
        if (o.unk12 != 0xFF) continue;
        if (o.unk6C[8].unk3 != o.unk12) continue;
        if (@as(c_int, o.unk5C) != a0) continue;
        if (@as(c_int, o.unk5E) != a1) continue;
        const e: c_int = o.unk1E;
        if (best < e) continue;
        if (best == e) tied = true;
        best = e;
        winner = i;
        found = true;
    }
    if (!found) return 0;
    if (tied) return 2;
    if (winner == 0) return 3;
    return 1;
}
