//! zig_diff spec: func_800D5780 -> func_800D5780.func_800D5780 (5BF94.c).
//! Inputs: a command with its index unk6 and count unk4.unk0_8 at their
//! edges (the last command, one before, past it, a negative index), its
//! kind 1, 13 or another; func_800D5700 and func_800D5738 are stubs.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D5780));

pub const classes = [_][:0]const u8{ "last", "kind-1", "kind-13", "other-kind" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const cmd = ctx.alloc(v.func_800D5780_t);
    const count = ctx.pick(u8, &.{ 0, 1, 2, 255 });
    psx.setBits(&cmd.unk4, "unk0_8", count);
    psx.setBits(&cmd.unk4, "unk0_0", ctx.pick(u8, &.{ 1, 1, 13, 13, 0, 12 }));
    cmd.unk6 = switch (ctx.range(u8, 0, 3)) {
        0 => @as(i16, count) - 1,
        1 => @as(i16, count) - 2,
        2 => -1,
        else => ctx.pick(i16, &.{ 0, 254, 32767 }),
    };
    return .{cmd};
}

pub fn classify(args: Args) usize {
    const cmd = args[0].?;
    if (@as(c_int, cmd.unk6) + 1 >= psx.getBits(&cmd.unk4, "unk0_8")) return 0;
    return switch (psx.getBits(&cmd.unk4, "unk0_0")) {
        1 => 1,
        13 => 2,
        else => 3,
    };
}
