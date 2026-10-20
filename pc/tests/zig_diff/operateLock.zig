//! zig_diff spec: func_8008D5FC -> room_lock.operateLock (146C.c).
//! Inputs: an arena lock (map section A) idle (unkB 0) or busy (1, 0x7F,
//! 0x80, 0xFF: the C tests it as a signed char), locked (state 0) or open
//! (any other s16). Both callees are recording stubs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").operateLock));

pub const classes = [_][:0]const u8{ "busy", "locked-queued", "open-toggled", "busy-negative" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const lock = ctx.alloc(v._mpdRoomSectionA);
    lock.unk0.unkB = if (ctx.chance(2)) 0 else ctx.pick(u8, &.{ 1, 0x7F, 0x80, 0xFF });
    lock.unk0.unk8.s16 = if (ctx.chance(2)) 0 else ctx.pick(i16, &.{ 1, -1, 0x100, -32768 });
    return .{lock};
}

pub fn classify(args: Args) usize {
    const lock = args[0].?;
    if (lock.unk0.unkB != 0) return if (lock.unk0.unkB >= 0x80) 3 else 0;
    return if (lock.unk0.unk8.s16 == 0) 1 else 2;
}
