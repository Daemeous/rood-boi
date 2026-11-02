//! zig_diff spec: func_800BE5A4 -> func_800BE5A4.func_800BE5A4 (4A0A8.c).
//! Inputs: the camera global points at an arena object whose lookAt and eye
//! durations are sometimes edge values; the argument covers 2 (the averaging
//! path), 0 and other values, both signs and the i16 extremes. The callee
//! stub writes the duration and mode of the block it is handed (the C reads
//! the duration back), and returns nothing.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BE5A4));

pub const classes = [_][:0]const u8{ "arg-two", "arg-zero", "arg-other" };

pub const stubs = struct {
    pub fn func_800BE53C(ctx: *zd.Ctx, p: anytype) void {
        const t: *v.D_800F4BA4_t2 = @ptrCast(@alignCast(p));
        if (ctx.value(u1) == 1) t.duration = ctx.value(u8);
        if (ctx.value(u1) == 1) t.mode = ctx.value(u8);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F4BA4_t);
    v.g.D_800F4BA4().* = obj;
    if (ctx.chance(3)) obj.lookAt.duration = ctx.pick(u8, &.{ 0, 1, 127, 128, 255 });
    if (ctx.chance(3)) obj.eye.duration = ctx.pick(u8, &.{ 0, 1, 127, 128, 255 });
    const arg = ctx.pick(c_short, &.{ 0, 1, 2, 2, 2, -1, -2, 32767, -32768 });
    return .{arg};
}

pub fn classify(args: Args) usize {
    const arg = args[0];
    if (arg == 2) return 0;
    if (arg == 0) return 1;
    return 2;
}
