//! zig_diff spec: func_800D65D8 -> func_800D65D8.func_800D65D8 (5BF94.c).
//! Inputs: a random script whose unkC target kind is 4 or another, its id
//! 0, 1, 255, 256 or -1; the value 0 or not; func_8009F898 is a stub.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D65D8));

pub const classes = [_][:0]const u8{ "kind-4-value", "kind-4-zero", "other-kind" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const s = ctx.alloc(v.D_800F53B8_t);
    s.unkD1C.unkC.unk0 = ctx.pick(u16, &.{ 4, 4, 5, 0, 0xFFFF });
    s.unkD1C.unkC.unk4.unk0 = ctx.pick(i32, &.{ 0, 1, 255, 256, -1 });
    return .{ s, ctx.pick(u8, &.{ 0, 0, 1, 255 }) };
}

pub fn classify(args: Args) usize {
    if (args[0].?.unkD1C.unkC.unk0 != 4) return 2;
    return if (args[1] != 0) 0 else 1;
}
