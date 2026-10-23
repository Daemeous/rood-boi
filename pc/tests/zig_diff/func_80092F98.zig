//! zig_diff spec: func_80092F98 -> func_80092F98.func_80092F98 (2842C.c).
//! Inputs: arg1 is always a valid func_80092F74_t2 in the arena with a random
//! unk4[0].s32 (edges 0, -1, 1, INT_MAX, INT_MIN half the time, any value
//! otherwise), so the call's argument differs in its high and low halves and
//! its sign. arg0 is never read by the C, so it is null or a valid object at
//! random. The C has no branches and no ordered comparisons; the callee
//! func_80091998 is a recording stub returning any c_int, and the C ignores it.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80092F98));

pub const classes = [_][:0]const u8{ "arg1-negative", "arg1-zero", "arg1-positive" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg1 = ctx.alloc(v.func_80092F74_t2);
    arg1.unk4[0].s32 = ctx.pick(i32, &.{ 0, -1, 1, 0x7FFF_FFFF, -0x8000_0000 });
    const arg0: ?*v.func_80092F74_t = if (ctx.chance(2)) null else ctx.alloc(v.func_80092F74_t);
    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    const s = args[1].?.unk4[0].s32;
    if (s < 0) return 0;
    if (s == 0) return 1;
    return 2;
}
