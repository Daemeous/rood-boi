//! zig_diff spec: func_800BB788 -> func_800BB788.func_800BB788 (4A0A8.c).
//! Inputs: one func_800BB68C_t in the arena with a random unk0 (0, 1, 2, 0x7F,
//! 0x80, 0xFF and any byte, so both sign-bit states occur), a random unk1 with
//! the edges 0, 21, 22, 254 and 255 (255 wraps the post-increment; 22 and up read
//! past unk2 into the rest of the arena, which both runs share), and random unk2
//! bytes. The C has no callees and no globals, so there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BB788));

pub const classes = [_][:0]const u8{ "count-zero", "index-past-array", "in-range" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.func_800BB68C_t);
    obj.unk0 = ctx.pick(u8, &.{ 0, 1, 2, 0x7F, 0x80, 0xFF });
    obj.unk1 = ctx.pick(u8, &.{ 0, 21, 22, 254, 255 });
    if (ctx.chance(4)) obj.unk0 = 0;
    return .{obj};
}

pub fn classify(args: Args) usize {
    const obj = args[0].?;
    if (obj.unk0 == 0) return 0;
    if (obj.unk1 >= 22) return 1;
    return 2;
}
