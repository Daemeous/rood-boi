//! zig_diff spec: func_800ACF54 -> func_800ACF54.func_800ACF54 (40564.c).
//! Inputs: one D_800F45E0_t in the arena with every byte random. prepare sets
//! its 4-bit unkC_0 field to 0, 1, 7, 8 or 15, so the C's `!unkC_0` branch sees
//! zero and nonzero values, with and without bit 3. The C makes no calls and
//! reads no globals; its stores (the 8-byte copy at 0x1C into 0x4C, unk60,
//! unk1868 and unk52) are checked through the state comparison.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800ACF54));

pub const classes = [_][:0]const u8{ "bit-zero", "bit-nonzero" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F45E0_t);
    psx.setBits(actor, "unkC_0", ctx.pick(u4, &.{ 0, 1, 7, 8, 15 }));
    return .{actor};
}

pub fn classify(args: Args) usize {
    return if (psx.getBits(args[0].?, "unkC_0") == 0) 0 else 1;
}
