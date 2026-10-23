//! zig_diff spec: func_800E4CE8 -> func_800E4CE8.func_800E4CE8 (7C4E8.c).
//! Inputs: one func_800DEEA4_t2 in the arena, its random bytes kept except unk38,
//! which is -1 in a quarter of the runs and random otherwise. The C has no
//! branches and no calls; the value of unk38 before the store is the only
//! input that varies the outcome, so both classes are declared. A null
//! argument is never passed: the C stores through it without a guard.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_7C4E8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E4CE8));

pub const classes = [_][:0]const u8{ "already-minus-one", "other-value" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.func_800DEEA4_t2);
    if (ctx.chance(4)) obj.unk38 = -1;
    return .{obj};
}

pub fn classify(args: Args) usize {
    return if (args[0].?.unk38 == -1) 0 else 1;
}
