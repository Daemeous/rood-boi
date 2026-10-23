//! zig_diff spec: func_800A92B8 -> func_800A92B8.func_800A92B8 (40564.c).
//! Inputs: the sixteen D_800F45E0 slots, each null or a fresh arena object.
//! Most objects are "good" (hidden flag, unk9 nibble and unk5C/unk5E matching
//! arg0/arg1 truncated, unk1A not 0xFE), so the minimum search runs; the rest
//! fail a test at random. unk1E takes the edges 3000 (the initial best, so
//! ties), 128 (the `- 128` lands on 0), 129 (the `> 0` clamp), 127, and in one
//! mode only non-negative values, in the other any i16. arg0/arg1 span 0..3,
//! -1 and 256, so the u8 compares see both matches and mismatches. The C makes
//! no external calls, so there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A92B8));

pub const classes = [_][:0]const u8{ "no-candidate", "best-at-most-128", "best-above-128" };

fn eligible(t: *v.D_800F45E0_t, arg0: c_int, arg1: c_int) bool {
    return psx.getBits(t, "unk8_0") == 0 and
        @as(c_int, t.unk5C) == arg0 and
        @as(c_int, t.unk5E) == arg1 and
        psx.getBits(t, "unk9_0") == 0 and
        @as(c_int, t.unk1A) != 0xFE;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0: c_int = ctx.pick(c_int, &.{ 0, 1, 2, 3, -1, 256 });
    const arg1: c_int = ctx.pick(c_int, &.{ 0, 1, 2, 3, -1, 256 });
    const positive = ctx.chance(2);
    const slots = v.g.D_800F45E0();

    for (0..16) |i| {
        if (ctx.chance(6)) {
            slots[i] = null;
            continue;
        }
        const t = ctx.alloc(v.D_800F45E0_t);
        slots[i] = t;

        const good = !ctx.chance(4);
        if (good) {
            psx.setBits(t, "unk8_0", 0);
            psx.setBits(t, "unk9_0", 0);
            t.unk5C = @truncate(@as(u32, @bitCast(arg0)));
            t.unk5E = @truncate(@as(u32, @bitCast(arg1)));
            t.unk1A = if (ctx.chance(5)) 0xFE else ctx.pick(i16, &.{ 0, -1, 100 });
        } else {
            psx.setBits(t, "unk8_0", ctx.int(u1));
            psx.setBits(t, "unk9_0", ctx.pick(u4, &.{ 0, 1, 15 }));
            t.unk5C = ctx.pick(u8, &.{ 0, 1, 2, 3, 128, 255 });
            t.unk5E = ctx.pick(u8, &.{ 0, 1, 2, 3, 128, 255 });
            t.unk1A = ctx.pick(i16, &.{ 0xFE, 0, -1 });
        }

        t.unk1E = if (positive)
            ctx.edge(i16, &.{ 3000, 128, 129, 2999, 200, 127, 500, 3000 })
        else
            ctx.pick(i16, &.{ 3000, 128, 129, 2999, 0, -1, 32767, -32768 });
    }

    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    const slots = v.g.D_800F45E0();
    var best: c_int = 0xBB8;
    var found = false;
    for (0..16) |i| {
        const t = slots[i] orelse continue;
        if (!eligible(t, args[0], args[1])) continue;
        if (@as(c_int, t.unk1E) < best) {
            best = t.unk1E;
            found = true;
        }
    }
    if (!found) return 0;
    return if (best - 128 > 0) 2 else 1;
}
