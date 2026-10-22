//! zig_diff spec: func_800D9538 -> func_800D9538.func_800D9538 (6FC00.c).
//! Inputs: the unit has no arguments and no branches. Its only data is the
//! scratchpad word at 0x1F8003F8, which prepare sets to edge values (0, -1,
//! the sign-bit-only and the maximum i32) about half the time and leaves as
//! random bytes otherwise. The C copies that word into D_800F58C4, a global
//! the harness randomises and compares, so a wrong address, a narrowed read
//! or a dropped store all change the compared state.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D9538));

pub const classes = [_][:0]const u8{ "zero", "minus-one", "sign-bit-only", "int-max", "other" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const word: *align(1) i32 = @ptrFromInt(zd.scratchpad + 0x3F8);
    if (ctx.chance(2)) {
        word.* = ctx.pick(i32, &.{ 0, -1, std.math.minInt(i32), std.math.maxInt(i32) });
    }
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const word: *align(1) i32 = @ptrFromInt(zd.scratchpad + 0x3F8);
    return switch (word.*) {
        0 => 0,
        -1 => 1,
        std.math.minInt(i32) => 2,
        std.math.maxInt(i32) => 3,
        else => 4,
    };
}
