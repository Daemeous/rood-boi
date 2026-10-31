//! zig_diff spec: func_800B13CC -> func_800B13CC.func_800B13CC (44F14.c).
//! Inputs: the 16-slot table D_800F45E0 holds arena objects or nulls. Each
//! object has its unk8_0 and unk6C[8].unk0_3 flags set about a quarter of the
//! time, so every && term is both true and false. Its unk5C and unk5E equal
//! arg0 and arg1 about half the time, and arg0/arg1 use the byte edges 0, 1,
//! 255, 256 and -1 (256 and -1 never match a u8). Its unk1E is a random i16
//! or arg2 itself, from edges including -32768, -1, 0, 127, 128, 129, 3000 and
//! 32767, so both comparisons see equal sides and the result lands in and out
//! of the 128 window. arg2 is drawn from -128, -1, 0, 127, 128, 129, 3000 and
//! 3001.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_44F14");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B13CC));

pub const classes = [_][:0]const u8{ "no-candidate", "candidate-above-128", "candidate-at-or-below-128" };

/// A C-style narrowing of an edge value to a byte (wraps, never checked).
fn byteFor(ctx: *zd.Ctx, arg: c_int) u8 {
    if (arg >= 0 and arg <= 255 and ctx.chance(2)) return psx.conv(u8, arg);
    return ctx.int(u8);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0: c_int = ctx.pick(c_int, &.{ 0, 1, 255, 256, -1 });
    const arg1: c_int = ctx.pick(c_int, &.{ 0, 1, 255, 256, -1 });
    const arg2: c_int = ctx.pick(c_int, &.{ -128, -1, 0, 127, 128, 129, 3000, 3001 });

    const table = v.g.D_800F45E0();
    for (table) |*slot| {
        if (ctx.chance(6)) {
            slot.* = null;
            continue;
        }
        const obj = ctx.alloc(v.D_800F45E0_t);
        const flag0: u1 = if (ctx.chance(4)) 1 else 0;
        const flag1: u1 = if (ctx.chance(4)) 1 else 0;
        psx.setBits(obj, "unk8_0", flag0);
        psx.setBits(&obj.unk6C[8], "unk0_3", flag1);
        obj.unk5C = byteFor(ctx, arg0);
        obj.unk5E = byteFor(ctx, arg1);
        const near: i16 = psx.conv(i16, arg2);
        const edge: i16 = ctx.pick(i16, &.{ -32768, -1, 0, 1, 127, 128, 129, 3000, 32767 });
        obj.unk1E = if (ctx.chance(2)) near else edge;
        slot.* = obj;
    }

    return .{ arg0, arg1, arg2 };
}

pub fn classify(args: Args) usize {
    const arg0 = args[0];
    const arg1 = args[1];
    const arg2 = args[2];
    var best: c_int = 0xBB8;
    var found = false;
    for (v.g.D_800F45E0().*) |slot| {
        const t = slot orelse continue;
        if (psx.getBits(t, "unk8_0") != 0) continue;
        if (psx.getBits(&t.unk6C[8], "unk0_3") != 0) continue;
        if (@as(c_int, t.unk5C) != arg0 or @as(c_int, t.unk5E) != arg1) continue;
        const h: c_int = t.unk1E;
        if (arg2 < h and h < best) {
            best = h;
            found = true;
        }
    }
    if (!found) return 0;
    return if (best - 128 > 0) 1 else 2;
}
