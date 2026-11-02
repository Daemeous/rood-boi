//! zig_diff spec: func_800BE36C -> func_800BE36C.func_800BE36C (4A0A8.c).
//! Inputs: the two int arguments (zero, negative and positive values, the
//! extremes included) and a camera object in the arena that D_800F4BA4
//! points at, with random bytes in every field. The C has no branches and
//! no calls, so the spec's job is to compare every byte the four stores
//! touch and to make sure the stores differ from the random fill.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BE36C));

pub const classes = [_][:0]const u8{ "arg-zero", "arg-negative", "arg-positive" };

pub fn prepare(ctx: *zd.Ctx) Args {
    // The C dereferences D_800F4BA4 without a null test: it must point at a
    // valid object.
    const cam = ctx.alloc(v.D_800F4BA4_t);
    v.g.D_800F4BA4().* = cam;
    const edges = [_]c_int{ 0, -1, 1, 255, 256, 0x7FFF, std.math.minInt(c_int), std.math.maxInt(c_int) };
    return .{ ctx.pick(c_int, &edges), ctx.pick(c_int, &edges) };
}

pub fn classify(args: Args) usize {
    if (args[0] == 0) return 0;
    if (args[0] < 0) return 1;
    return 2;
}
