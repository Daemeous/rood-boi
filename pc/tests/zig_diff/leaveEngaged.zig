//! zig_diff spec: func_800DC19C -> pc.leave_engaged.leaveEngaged (D2: pc/src/battle_movement.c).
//! Inputs: a random-filled state block with the engaged flag (0x13) clear or
//! set, the saved callback (0x6C) zero or any word, and the flag 0x30 clear or
//! set, so every path of the recovery occurs. The callees (func_800D820C,
//! func_800E7370, func_800D8260, func_800E48A8) are recording stubs: their
//! arguments (the actor id byte 0x88, the constants) and order are compared.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").leaveEngaged));

pub const classes = [_][:0]const u8{ "idle", "recover", "recover-no-saved", "skip-recovery" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.allocBytes(0x200, 16);
    for (block[0..0x200]) |*byte| byte.* = ctx.int(u8);
    block[0x13] = ctx.edge(u8, &.{ 0, 0, 1, 0x80 });
    block[0x30] = ctx.edge(u8, &.{ 0, 0, 1, 0x40 });
    const saved: *align(1) u32 = @ptrCast(block + 0x6c);
    if (ctx.chance(3)) saved.* = 0;
    return .{block};
}

pub fn classify(args: Args) usize {
    const block = args[0];
    if (block[0x13] == 0) return 0;
    if (block[0x30] != 0) return 3;
    return if (@as(*align(1) const u32, @ptrCast(block + 0x6c)).* == 0) 2 else 1;
}
