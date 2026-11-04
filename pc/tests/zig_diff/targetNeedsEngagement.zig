//! zig_diff spec: func_800DBC60 -> pc.target_needs_engagement.targetNeedsEngagement (D2: pc/src/battle_movement.c).
//! Inputs: a state block whose byte 0x13 is 0 or set; a candidate record of
//! five words with option word 4 bit 4 set or clear and target (word 3, low
//! nibble) 0..15 (the other bits random); the engagement table D_800F45E0
//! with each of its 16 slots empty or set.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_movement");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").targetNeedsEngagement));

pub const classes = [_][:0]const u8{ "busy", "no-engagement-needed", "target-engaged", "target-free" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.allocBytes(0x40, 16);
    for (state[0..0x40]) |*byte| byte.* = ctx.int(u8);
    state[0x13] = if (ctx.chance(3)) ctx.range(u8, 1, 255) else 0;
    const candidate = ctx.allocBytes(0x20, 16);
    for (candidate[0..0x20]) |*byte| byte.* = ctx.int(u8);
    if (ctx.chance(3)) candidate[16] &= 0xEF else candidate[16] |= 0x10;
    const table = v.g.D_800F45E0();
    for (0..16) |i| table[i] = if (ctx.chance(2)) null else @ptrCast(ctx.allocBytes(0x10, 16));
    return .{ state, candidate };
}

pub fn classify(args: Args) usize {
    if (args[0][0x13] != 0) return 0;
    if (args[1][16] & 0x10 == 0) return 1;
    return if (v.g.D_800F45E0()[args[1][12] & 0xf] == null) 3 else 2;
}
