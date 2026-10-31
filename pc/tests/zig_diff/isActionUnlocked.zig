//! zig_diff spec: vs_battle_isActionUnlocked -> is_action_unlocked.isActionUnlocked (146C.c).
//! Inputs: an arg0 drawn from the edges of the unsigned `arg0 - 1 < 0xFF` test
//! (0, 1, 254, 255, 256, 257, 0xFFFFFFFF, 0x80000000/1) and random values. The
//! action table is random and, for in-range indices, its unlocked bit is set or
//! cleared at random, so both return values occur at every index. Index 256 makes
//! the test's two sides equal (255 == 0xFF). No callees: the body is one global read.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").isActionUnlocked));

pub const classes = [_][:0]const u8{ "zero", "last-valid", "in-range", "boundary-256", "huge" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0 = ctx.pick(c_uint, &.{ 0, 1, 2, 128, 254, 255, 256, 257, 0xFFFF_FFFF, 0x8000_0000, 0x8000_0001 });
    if (arg0 < 256 and ctx.chance(2)) {
        const actions = v.g.actions();
        psx.setBits(&actions[arg0], "unlocked", ctx.int(u1));
    }
    return .{arg0};
}

pub fn classify(args: Args) usize {
    const a = args[0];
    if (a == 0) return 0;
    if (a == 255) return 1;
    if (a >= 1 and a <= 254) return 2;
    if (a == 256) return 3;
    return 4;
}
