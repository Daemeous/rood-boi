//! zig_diff spec: vs_battle_script_setCurrentSfx -> script_set_current_sfx.script_setCurrentSfx (4A0A8.c).
//!
//! Inputs: a 4-byte buffer in the arena whose byte 1 is the sound id, drawn
//! from the edges 0, 127, 128 and 255 (half the time) or any byte, so both
//! the high-bit and low-bit ids reach the call. arg1 is an ignored short
//! drawn from its edges. The C has no branches; the call to
//! vs_main_setCurrentSfx is a recording stub whose return value the C
//! discards.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").script_setCurrentSfx));

pub const classes = [_][:0]const u8{ "high-byte", "low-byte" };

pub const stubs = struct {
    pub fn vs_main_setCurrentSfx(ctx: *zd.Ctx, id: anytype) c_int {
        _ = id;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf: [*c]u8 = @ptrCast(ctx.allocBytes(4, 4));
    buf[1] = ctx.pick(u8, &.{ 0, 127, 128, 255 });
    return .{ buf, ctx.pick(c_short, &.{ 0, -1, 32767, -32768 }) };
}

pub fn classify(args: Args) usize {
    return if (args[0][1] >= 128) 0 else 1;
}
