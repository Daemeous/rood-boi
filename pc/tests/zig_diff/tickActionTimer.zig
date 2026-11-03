//! zig_diff spec: func_800D8280 -> pc.tick_action_timer.tickActionTimer (D2: pc/src/battle_helpers.c).
//! Inputs: a random-filled state block whose byte 0x12A is 0 (idle), 1 (the
//! tick that sends 0) or more (the tick that sends the halfword at 0x138, which
//! is 0 or any value, so a send of 0 from the second path is distinct from the
//! first); byte 0x88 (the actor) is any byte, 0 included. The stub of
//! func_800D820C records (actor, value).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").tickActionTimer));

pub const classes = [_][:0]const u8{ "idle", "reaches-zero", "counting" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.allocBytes(0x200, 16);
    for (block[0..0x200]) |*byte| byte.* = ctx.int(u8);
    block[0x12a] = ctx.edge(u8, &.{ 0, 1, 1, 2, 3, 0xFF });
    block[0x88] = ctx.edge(u8, &.{ 0, 1, 5, 0x10, 0xFF });
    std.mem.writeInt(u16, block[0x138..][0..2], ctx.edge(u16, &.{ 0, 1, 0x100, 0x8000, 0xFFFF }), .little);
    return .{block};
}

pub fn classify(args: Args) usize {
    return switch (args[0][0x12a]) {
        0 => 0,
        1 => 1,
        else => 2,
    };
}
