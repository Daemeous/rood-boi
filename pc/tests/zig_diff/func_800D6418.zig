//! zig_diff spec: func_800D6418 -> func_800D6418.func_800D6418 (5BF94.c).
//! Inputs: a random script whose unk18 target kind is 5 or another;
//! func_8006CDD8 is a recording stub (its pointer argument and the state hash).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D6418));

pub const classes = [_][:0]const u8{ "kind-5", "other-kind" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const s = ctx.alloc(v.D_800F53B8_t);
    s.unkD1C.unk18.unk0 = ctx.pick(u16, &.{ 5, 5, 4, 5, 0, 0xFFFF });
    s.unkD1C.unk18.unk4.unk0 = ctx.pick(i32, &.{ 0, 1, 255, 256, -1 });
    return .{s};
}

pub fn classify(args: Args) usize {
    return if (args[0].?.unkD1C.unk18.unk0 == 5) 0 else 1;
}
