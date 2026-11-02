//! zig_diff spec: func_800B83D4 -> func_800B83D4.func_800B83D4 (4A0A8.c).
//! Inputs: one 8-byte command block in the arena. The top two bits of byte 6
//! pick the D_800E9B58 entry (all four, from edge values and random bytes),
//! its low six bits are the second argument, byte 3 is added to the table
//! entry, byte 4 is the fourth argument, and bytes 1-2 (the short the getter
//! reads) are random, so negative values occur. The callees are default
//! recording stubs: vs_battle_getShort returns a random c_short, and
//! func_800BFE50 and func_800A0618 record their arguments and return the
//! default values. The C has no branches, so the classes are the table index.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B83D4));

pub const classes = [_][:0]const u8{ "index-0", "index-1", "index-2", "index-3" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const cmd = ctx.allocBytes(8, 16);
    cmd[1] = ctx.int(u8);
    cmd[2] = ctx.int(u8);
    cmd[3] = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
    cmd[4] = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
    cmd[5] = ctx.int(u8);
    cmd[6] = ctx.pick(u8, &.{ 0x00, 0x3F, 0x40, 0x7F, 0x80, 0xC0, 0xFF });
    cmd[7] = ctx.int(u8);
    return .{cmd};
}

pub fn classify(args: Args) usize {
    return @as(usize, args[0][6] >> 6);
}
