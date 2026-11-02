//! zig_diff spec: func_800BBAFC -> func_800BBAFC.func_800BBAFC (4A0A8.c).
//! Inputs: the D_800F4B88 block with random bytes, biased so unk3 is zero (the
//! skip), unk3 equal to the stepped unk2 (the reset), unk2 wrapping from 255 to 0,
//! and unk3 = 1 with unk2 in 7..254 so the (x << 12) / 1 quotient overflows the
//! short temp_lo. The two callees are recording stubs. func_8009134C may rewrite
//! unk2 and func_80091320 may rewrite unk3, and the C's compare reads both back.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BBAFC));

pub const classes = [_][:0]const u8{ "skip", "reset", "step-wrap", "step", "short-wrap" };

pub const stubs = struct {
    pub fn func_8009134C(ctx: *zd.Ctx, a: anytype, b: anytype) void {
        _ = a;
        _ = b;
        // The callee may move the stepped counter; the C compares it after the calls.
        if (ctx.value(u8) < 64) v.g.D_800F4B88().unk2 = ctx.value(u8);
    }

    pub fn func_80091320(ctx: *zd.Ctx, a: anytype) void {
        _ = a;
        // The callee may move the target count; the C compares it after the calls.
        if (ctx.value(u8) < 32) v.g.D_800F4B88().unk3 = ctx.value(u8);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const d = v.g.D_800F4B88();
    d.unk2 = ctx.pick(u8, &.{ 0, 7, 254, 255 });
    d.unk3 = ctx.pick(u8, &.{ 0, 1, 2, 3, 255 });
    if (ctx.chance(3)) d.unk3 = d.unk2 +% 1; // equal after the increment
    d.unk6 = ctx.pick(i16, &.{ 0, 1, -1, 4095, 4096, 32767, -32768 });
    d.unk8 = ctx.pick(i16, &.{ 0, 1, -1, 4095, 4096, 32767, -32768 });
    d.unkC = ctx.pick(i16, &.{ 0, 1, -1, 4095, 4096, 32767, -32768 });
    d.unkE = ctx.pick(i16, &.{ 0, 1, -1, 4095, 4096, 32767, -32768 });
    d.unk12 = ctx.pick(i16, &.{ 0, 1, -1, 4095, 4096, 32767, -32768 });
    d.unk14 = ctx.pick(i16, &.{ 0, 1, -1, 4095, 4096, 32767, -32768 });
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const d = v.g.D_800F4B88();
    if (d.unk3 == 0) return 0;
    if (d.unk3 == 1 and d.unk2 >= 7 and d.unk2 != 255) return 4;
    if (d.unk2 == 255) return 2;
    if (d.unk2 +% 1 == d.unk3) return 1;
    return 3;
}
