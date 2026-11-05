//! zig_diff spec: func_800DC210 -> pc.enter_engaged.enterEngaged (D2: pc/src/battle_movement.c).
//! Inputs: a random-filled state block, `force` 0, 1 or any, the roll counter
//! (0x126) at 0, 1, 0xFF or random; `rand` returns a roll below 0x4C, 0x4C
//! itself, above it, or a word whose low 7 bits decide. The callee
//! func_800D820C is a recording stub (the id byte 0x88 and 0x80 compared);
//! the installed callback word is behaviour_800DBC0C's address on both sides.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").enterEngaged));

pub const classes = [_][:0]const u8{ "forced", "roll-fails", "first-roll", "second-roll" };

var roll: c_int = 0;

pub const stubs = struct {
    pub fn rand(ctx: *zd.Ctx) c_int {
        _ = ctx;
        return roll;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.allocBytes(0x200, 16);
    for (block[0..0x200]) |*byte| byte.* = ctx.int(u8);
    block[0x126] = ctx.pick(u8, &.{ 0, 1, 0xFF, 2 });
    roll = ctx.pick(c_int, &.{ 0, 0x4B, 0x4C, 0x4D, 0x7F, 0x80, 0xCB, -1 });
    const force = ctx.edge(c_int, &.{ 0, 0, 0, 1, -1, 2 });
    return .{ block, force };
}

pub fn classify(args: Args) usize {
    if (args[1] != 0) return 0;
    if ((@as(c_uint, @bitCast(roll)) & 0x7f) >= 0x4c) return 1;
    return if (args[0][0x126] == 0 or args[0][0x126] == 0xFF) 2 else 3;
}
