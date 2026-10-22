//! zig_diff spec: vs_battle_setEffectExec -> set_effect_exec.setEffectExec (6F014.c).
//! Inputs: the effect renderer, null or an arbitrary function address (never called,
//! only stored), and an index in 0..7, the table's eight entries. The C has no branch
//! and no comparison, so the spec covers the store itself: every index, both null and
//! non-null renderers. Indices outside 0..7 are left out because the table has no room
//! for them.
const std = @import("std");
const zd = @import("zdiff");
const c = @import("c_6F014");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").setEffectExec));

pub const classes = [_][:0]const u8{ "null-func", "fn-func" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const func: c.effectExec = if (ctx.chance(4)) null else @ptrFromInt(ctx.int(usize));
    const index = ctx.pick(c_int, &.{ 0, 7 });
    return .{ func, if (index < 0 or index > 7) ctx.range(c_int, 0, 7) else index };
}

pub fn classify(args: Args) usize {
    return if (args[0] == null) 0 else 1;
}
