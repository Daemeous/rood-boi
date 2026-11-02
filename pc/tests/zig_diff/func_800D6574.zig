//! zig_diff spec: func_800D6574 -> func_800D6574.func_800D6574 (5BF94.c).
//! Inputs: a random script whose unk0 target kind is 4 or another, its
//! id 0, 1, 255, 256 or -1 (the low byte is passed); func_8006CDB8 is a stub.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D6574));

pub const classes = [_][:0]const u8{ "kind-4", "other-kind" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const s = ctx.alloc(v.D_800F53B8_t);
    s.unkD1C.unk0.unk0 = ctx.pick(u16, &.{ 4, 4, 4, 5, 0, 0xFFFF });
    s.unkD1C.unk0.unk4.unk0 = ctx.pick(i32, &.{ 0, 1, 255, 256, -1 });
    return .{s};
}

pub fn classify(args: Args) usize {
    return if (args[0].?.unkD1C.unk0.unk0 == 4) 0 else 1;
}
