//! zig_diff spec: func_800D6628 -> func_800D6628.func_800D6628 (5BF94.c).
//! Inputs: the script's unkC target kind 4 or another, its id 0, 255, 256
//! or -1; the variant 0, 3 (given), 4 or 255 (random); the last variant
//! D_800EC4B8 0..2 (the draw repeats while it matches) or another; rand is a
//! stub returning 0..32767.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D6628));

pub const classes = [_][:0]const u8{ "other-kind", "given", "random" };

pub const stubs = struct {
    pub fn rand(ctx: *zd.Ctx) c_int {
        return ctx.value(u15);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const s = ctx.alloc(v.D_800F53B8_t);
    s.unkD1C.unkC.unk0 = ctx.pick(u16, &.{ 4, 4, 4, 5 });
    s.unkD1C.unkC.unk4.unk0 = ctx.pick(i32, &.{ 0, 255, 256, -1 });
    v.g.D_800EC4B8().* = ctx.pick(u8, &.{ 0, 1, 2, 2 });
    return .{ s, ctx.pick(u8, &.{ 0, 3, 4, 4, 255 }) };
}

pub fn classify(args: Args) usize {
    if (args[0].?.unkD1C.unkC.unk0 != 4) return 0;
    return if (args[1] < 4) 1 else 2;
}
