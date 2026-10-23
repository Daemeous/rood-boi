//! zig_diff spec: func_8009291C -> func_8009291C.func_8009291C (2842C.c).
//! Inputs: arg0 in 0..95 (the 96-entry D_800F1DD8 table; negative values are
//! not generated, since the C's first branch would write the entry before the
//! table), with the thresholds D_800F225A and D_800F225B+0x40 as random bytes
//! with edge values, so arg0 lands below, at, and above each bound. The
//! targeted entry gets a flag (unk0) with and without the sign bit, and its
//! two pointers point at arena objects, either the same one or two different
//! ones, so the C's dereferences stay valid. The callee func_800927AC rewrites
//! the entry's flag, which the C never reads back but the state comparison sees.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009291C));

pub const classes = [_][:0]const u8{
    "below-threshold",
    "low-zero",
    "above-window",
    "live-flag",
    "pointer-differs",
    "clean-copy",
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const thr = ctx.pick(u8, &.{ 0, 1, 63, 64, 96, 128, 255 });
    const win = ctx.pick(u8, &.{ 0, 1, 31, 32, 255 });
    v.g.D_800F225A().* = thr;
    v.g.D_800F225B().* = win;

    const cand = [_]c_int{
        @as(c_int, thr),
        @as(c_int, win) + 0x40,
        0x40,
        0x3F,
        0,
        95,
    };
    const arg0: c_int = if (ctx.chance(2))
        @min(ctx.edge(c_int, &cand), @as(c_int, 95))
    else
        ctx.range(c_int, 0, 95);

    const e = &v.g.D_800F1DD8()[@intCast(arg0)];
    e.unk0 = ctx.pick(u8, &.{ 0, 0, 1, 0x80, 0xFF });

    // The C dereferences both pointers whenever the flag is clear, so they
    // must point at valid objects: the same one half the time, else two.
    const a = ctx.alloc(v.D_800F1DD8_t2);
    psx.put(&e.unk4, a);
    if (ctx.chance(2)) {
        psx.put(&e.unk8, a);
    } else {
        psx.put(&e.unk8, ctx.alloc(v.D_800F1DD8_t2));
    }

    return .{arg0};
}

pub fn classify(args: Args) usize {
    const arg0 = args[0];
    const thr: c_int = v.g.D_800F225A().*;
    const win: c_int = @as(c_int, v.g.D_800F225B().*) + 0x40;
    if (arg0 < thr) return 0;
    if (arg0 < 0x40) return 1;
    if (arg0 >= win) return 2;
    const e = v.g.D_800F1DD8()[@intCast(arg0)];
    if (e.unk0 != 0) return 3;
    if (psx.get(e.unk4) != psx.get(e.unk8)) return 4;
    return 5;
}

pub const stubs = struct {
    /// Rewrites the entry's flag: the C sets it to 1 before the call, so the
    /// callee's write must be ordered after that store.
    pub fn func_800927AC(ctx: *zd.Ctx, p: anytype) void {
        const e: *v.D_800F1DD8_t = @ptrCast(@alignCast(p));
        e.unk0 = ctx.value(u8);
    }
};
