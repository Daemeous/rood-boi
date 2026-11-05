//! zig_diff spec: pc_charge_skip_window -> charge_skip.window (146C.c, charge_skip.inc).
//! Inputs: the action block present or not, BATTLE's mode 2 or not, the ability window (any u16).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").window));

pub const classes = [_][:0]const u8{ "no-block", "other-mode", "window" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.alloc(v.D_800F19CC_t);
    v.g.D_800F19CC().* = if (ctx.chance(4)) null else block;
    v.g.D_800F196C().* = ctx.edge(u32, &.{ 2, 2, 1, 0 });
    block.battleAbilityTargetWindow = ctx.pick(u16, &.{ 0, 1, 0xFFFF, 0x8000 });
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    if (v.g.D_800F19CC().* == null) return 0;
    return if (v.g.D_800F196C().* == 2) 2 else 1;
}
