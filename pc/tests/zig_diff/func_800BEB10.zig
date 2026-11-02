//! zig_diff spec: func_800BEB10 -> func_800BEB10.func_800BEB10 (4A0A8.c).
//! The C has no branches and no comparisons: it returns (D_800F4BE0 << 12) | _evtFile,
//! truncated to a short. The inputs set D_800F4BE0 and _evtFile to edge values
//! (0, 0xFFFF, 0x8000, 0x000F, and for _evtFile -1, -32768, 32767, 0x07FF, 0) half the
//! time and leave random bytes otherwise, so the shift overflow, the sign-extended
//! low term and the high bits of the u16 global are all exercised. Inputs are
//! classed by the sign of the low term, since a negative _evtFile fills the high
//! bits of the result through sign extension.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BEB10));

pub const classes = [_][:0]const u8{ "low-term-negative", "low-term-nonnegative" };

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g.D_800F4BE0().* = ctx.pick(u16, &.{ 0, 0xFFFF, 0x8000, 0x000F, 0x0001 });
    v.g._evtFile().* = ctx.pick(i16, &.{ -1, -32768, 32767, 0x07FF, 0 });
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (v.g._evtFile().* < 0) 0 else 1;
}
