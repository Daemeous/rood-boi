//! zig_diff spec: vs_battle_setActionUnlocked -> set_action_unlocked.setActionUnlocked (146C.c).
//! Inputs: an arg0 drawn from the edges of the unsigned test `(arg0 - 1) < 0xFF`:
//! 0 (wraps to 0xFFFFFFFF), 1, middle values 2..254, 255 (the last index that
//! is written), 256 (the comparison's two sides are equal), values above 256
//! and the top of the u_int range. No callees, no globals to prepare: the
//! unit writes only vs_main_actions[arg0].unlocked, and arg0 is never out of
//! range when the store runs.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").setActionUnlocked));

pub const classes = [_][:0]const u8{ "zero", "one", "middle", "last-in-range", "at-256", "above-256" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0: c_uint = if (ctx.chance(4))
        ctx.range(c_uint, 2, 254)
    else
        ctx.pick(c_uint, &.{ 0, 1, 255, 256, 257, 0xFFFF_FFFF, 0x8000_0000, 0x7FFF_FFFF, 0xFFFF });
    return .{arg0};
}

pub fn classify(args: Args) usize {
    const a = args[0];
    if (a == 0) return 0;
    if (a == 1) return 1;
    if (a == 255) return 3;
    if (a == 256) return 4;
    if (a > 256) return 5;
    return 2;
}
