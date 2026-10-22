//! zig_diff spec: func_800D7FFC -> func_800D7FFC.func_800D7FFC (6F014.c).
//! The C is one store: D_800F5638.unk0 = arg0. The inputs are an arbitrary
//! 32-bit arg0, with the edge values (0, 1, -1, INT_MAX, INT_MIN) picked
//! half the time and random bits otherwise. Both signs are covered, so a
//! signed/unsigned or truncating slip shows. The global's other fields
//! (loadContexts, slots, isClut) keep their random bytes, so a store to the
//! wrong field or a dropped store is compared against the whole block.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_6F014");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D7FFC));

pub const classes = [_][:0]const u8{ "negative", "non-negative" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const x = ctx.pick(c_int, &.{ 0, 1, -1, 0x7FFF_FFFF, -0x8000_0000, 0xFFFF, -0x8000 });
    return .{x};
}

pub fn classify(args: Args) usize {
    return if (args[0] < 0) 0 else 1;
}
