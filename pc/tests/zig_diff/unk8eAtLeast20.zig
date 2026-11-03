//! zig_diff spec: func_800D826C -> pc.unk8e_at_least_20.unk8eAtLeast20 (D2: pc/src/battle_helpers.c).
//! Inputs: a random-filled context block in the arena whose halfword at 0x8E
//! is one of the values around the 0x20 bound (0x1F, 0x20, 0x21), 0, 1, or
//! the signed-bit edges (0x7FFF, 0x8000, 0xFFFF, which a signed read gets wrong).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").unk8eAtLeast20));

pub const classes = [_][:0]const u8{ "below-0x20", "at-or-above-0x20" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.allocBytes(0x100, 16);
    for (block[0..0x100]) |*byte| byte.* = ctx.int(u8);
    const value: u16 = ctx.edge(u16, &.{ 0, 1, 0x1F, 0x20, 0x21, 0x7FFF, 0x8000, 0xFFFF });
    std.mem.writeInt(u16, block[0x8e..][0..2], value, .little);
    return .{block};
}

pub fn classify(args: Args) usize {
    return @intFromBool(std.mem.readInt(u16, args[0][0x8e..][0..2], .little) >= 0x20);
}
