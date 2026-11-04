//! zig_diff spec: func_800E511C -> pc.ai_driver.runAiDriver (D2: pc/src/battle_helpers.c).
//! Inputs: the driver switch D_800F58B8 zero or nonzero, the active-actor
//! mask D_800F590C with at least one of its low 16 bits set (an empty mask
//! would spin forever after pc_unimplemented returns; the stub does not
//! stop, so that path is waived) and random upper bits, and the current
//! actor D_800F58D4 at 0..15, 16, any int or negative. The stubs of the five
//! driver steps record the call order.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_helpers");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").runAiDriver));

pub const classes = [_][:0]const u8{ "disabled", "single-actor", "several-actors" };

var which: usize = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    const enabled = !ctx.chance(4);
    v.g.D_800F58B8().* = if (enabled) ctx.pick(u32, &.{ 1, 0x80000000, 0xFFFFFFFF }) else 0;
    var low: u32 = 0;
    // Only the low 16 bits count: a value whose set bits are all above bit 15
    // would leave the search without a target and spin forever.
    while (low == 0) low = ctx.int(u32) & 0xFFFF & ctx.pick(u32, &.{ 0xFFFF, 0x8001, 1, 0x4000, 0x8000, 0x0F0F, 0xFFFF });
    if (ctx.chance(3)) low = @as(u32, 1) << ctx.range(u5, 0, 15);
    v.g.D_800F590C().* = (ctx.int(u32) & 0xFFFF0000) | low;
    v.g.D_800F58D4().* = ctx.pick(c_int, &.{ 0, 1, 14, 15, 16, -1, 0x7FFFFFFF, -0x80000000 });
    which = if (!enabled) 0 else if (@popCount(low) == 1) 1 else 2;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return which;
}
