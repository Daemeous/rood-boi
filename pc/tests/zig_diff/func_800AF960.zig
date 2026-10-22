//! zig_diff spec: func_800AF960 -> func_800AF960.func_800AF960 (44F14.c).
//! Inputs: one D_800F45E0_t in the arena with its unk1864 block random and biased
//! to edges. unk4 and unk5 (the two counters) cover 0, 1, 2, values with the sign
//! bit set (128, 255) and random bytes, so both `!= 0` tests see nonzero values
//! with and without the high bit. unkA, unk10 and unk12 are signed and take
//! negative, zero and positive edges. unk8, unkC and unkE are unsigned and take
//! values with bit 11 and bit 15 set, so the 0xFFF mask and the u16 wraps of
//! `+=` are exercised. The C has no calls and no globals.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_44F14");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AF960));

pub const classes = [_][:0]const u8{
    "neither",
    "unk4-only",
    "unk5-only",
    "count-of-one",
    "both-general",
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F45E0_t);
    const a = &obj.unk1864;
    a.unk4 = ctx.pick(u8, &.{ 0, 1, 2, 3, 128, 255 });
    a.unk5 = ctx.pick(u8, &.{ 0, 1, 2, 127, 128, 255 });
    a.unkA = ctx.pick(i16, &.{ -32768, -32767, -1, 0, 1, 32767 });
    a.unk10 = ctx.pick(i16, &.{ -32768, -32767, -1, 0, 1, 32767 });
    a.unk12 = ctx.pick(i16, &.{ -32768, -32767, -1, 0, 1, 32767 });
    a.unk8 = ctx.pick(u16, &.{ 0, 0xFFF, 0x1000, 0x800, 0xFFFF, 0xF000 });
    a.unkC = ctx.pick(u16, &.{ 0, 0xFFFF, 0xFFF0, 0x8000 });
    a.unkE = ctx.pick(u16, &.{ 0, 0xFFFF, 0xFFF0, 0x8000 });
    return .{obj};
}

pub fn classify(args: Args) usize {
    const a = &args[0].?.unk1864;
    if (a.unk4 == 0 and a.unk5 == 0) return 0;
    if (a.unk4 == 0) return 2;
    if (a.unk5 == 0) return 1;
    if (a.unk4 == 1 or a.unk5 == 1) return 3;
    return 4;
}
