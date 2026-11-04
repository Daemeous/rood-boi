//! zig_diff spec: func_800E48A8 -> pc.command_byte_request.requestCommandByte (D2: pc/src/battle_movement.c).
//! Inputs: a state block with a route word (0x18C) and a cooldown byte (0x155)
//! zero or not, the command block pointer (word at 0x54) to an arena block, a
//! value (any int, only its low byte is stored) and a range; the roll the
//! func_800E45D4 stub returns is below, equal to or above the range.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").requestCommandByte));

pub const classes = [_][:0]const u8{ "route", "cooldown", "roll-high", "set" };

var roll: c_int = 0;

pub const stubs = struct {
    pub fn func_800E45D4(ctx: *zd.Ctx, n: anytype) c_int {
        _ = ctx;
        _ = n;
        return roll;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.allocBytes(0x200, 16);
    for (block[0..0x200]) |*byte| byte.* = ctx.int(u8);
    const command = ctx.allocBytes(0x40, 16);
    for (command[0..0x40]) |*byte| byte.* = ctx.int(u8);
    @as(*align(1) u32, @ptrCast(block + 0x54)).* = @intCast(@intFromPtr(command));
    if (ctx.chance(2)) @as(*align(1) u32, @ptrCast(block + 0x18c)).* = 0;
    if (ctx.chance(2)) block[0x155] = 0;
    const range = ctx.pick(c_int, &.{ 1, 2, 0x80, 3, 0, -1, 0x7FFFFFFF, -0x80000000 });
    roll = switch (ctx.range(u8, 0, 3)) {
        0 => range -% 1,
        1 => range,
        2 => range +% 1,
        else => ctx.pick(c_int, &.{ 0, 0x7F, -1 }),
    };
    return .{ block, ctx.pick(c_int, &.{ 0, 1, 0x7F, 0x80, 0xFF, 0x100, -1, 0x12345678 }), range };
}

pub fn classify(args: Args) usize {
    const block = args[0];
    if (@as(*align(1) const u32, @ptrCast(block + 0x18c)).* != 0) return 0;
    if (block[0x155] != 0) return 1;
    return if (roll < args[2]) 3 else 2;
}
