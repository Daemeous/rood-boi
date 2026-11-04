//! zig_diff spec: func_800E4BD8 -> room_flag.clearRoomFlag (D2: pc/src/battle_movement.c, through its static room_flag).
//! Inputs: D_800F58BC null a sixth of the time, else the address of a 32-byte
//! arena room record whose byte 0xE is random, all ones or zero; the bit is
//! 0-7 half the time, else an edge of the `& 31` (8, 15, 30, 31, 32, 39, 63,
//! negative values, the int extremes) or any int. Bits 8-30 change nothing
//! in the byte, bit 31 inverts the C's sign test (a clear sets all ones).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_movement");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").clearRoomFlag));

pub const classes = [_][:0]const u8{ "no-room", "low-bit", "mid-bit", "bit-31" };

pub fn prepare(ctx: *zd.Ctx) Args {
    if (ctx.chance(6)) {
        v.g.D_800F58BC().* = 0;
    } else {
        const room = ctx.allocBytes(0x20, 16);
        for (room[0..0x20]) |*byte| byte.* = ctx.int(u8);
        room[0xe] = if (ctx.chance(3)) 0xff else if (ctx.chance(3)) 0 else ctx.int(u8);
        v.g.D_800F58BC().* = @intCast(@intFromPtr(room));
    }
    const bit: c_int = if (ctx.chance(2)) ctx.range(c_int, 0, 7) else ctx.pick(c_int, &.{ 8, 15, 30, 31, 32, 39, 63, -1, -32, 0x7FFFFFFF, -0x80000000 });
    return .{bit};
}

pub fn classify(args: Args) usize {
    if (v.g.D_800F58BC().* == 0) return 0;
    const low = args[0] & 31;
    if (low == 31) return 3;
    return if (low < 8) 1 else 2;
}
