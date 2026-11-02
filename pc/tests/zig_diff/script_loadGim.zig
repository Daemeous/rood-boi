//! zig_diff spec: vs_battle_script_loadGim -> script_load_gim.script_loadGim (4A0A8.c).
//! The C has no branches of its own: it reads a short at arg0+1 through
//! vs_battle_getShort, reads the byte at arg0[3], and calls vs_battle_loadGim
//! with both before returning 0. The inputs are a random byte block in the
//! arena (its byte 3 is 0, 1, 127, 128 or 255, so the signed and unsigned
//! reads differ) and a random arg1 (which the C ignores, so a port that
//! returns it fails). Classes split on the high bit of arg0[3].
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").script_loadGim));

pub const classes = [_][:0]const u8{ "byte3-high", "byte3-low" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const bytes = ctx.allocBytes(8, 1);
    bytes[3] = ctx.pick(u8, &.{ 0, 1, 127, 128, 255 });
    const arg1 = ctx.pick(c_short, &.{ 0, -1, 32767, -32768 });
    return .{ bytes, arg1 };
}

pub fn classify(args: Args) usize {
    return if (args[0][3] >= 128) 0 else 1;
}
