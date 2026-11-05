//! zig_diff spec: pc_charge_skip_ability -> charge_skip.ability (146C.c, charge_skip.inc).
//! Inputs: the action block present or not, BATTLE's mode 2 or not, and the block's timer,
//! window and input flags at any value.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").ability));

pub const classes = [_][:0]const u8{ "no-action", "action" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.alloc(v.D_800F19CC_t);
    v.g.D_800F19CC().* = if (ctx.chance(4)) null else block;
    v.g.D_800F196C().* = ctx.edge(u32, &.{ 2, 2, 2, 1, 0 });
    return .{ctx.alloc([4]c_int)};
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (v.g.D_800F19CC().* != null and v.g.D_800F196C().* == 2) 1 else 0;
}
