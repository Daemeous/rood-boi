//! zig_diff spec: vs_battle_script_clearMusicLoadSlot -> script_clear_music_load_slot.script_clearMusicLoadSlot (4A0A8.c).
//! The C has no branch of its own: it reads arg0[1], passes it to vs_main_clearMusicLoadSlot and
//! returns whether the result is 1. The inputs are a 4-byte arena buffer whose second byte is a
//! random value, biased toward the edges 0, 1, 0x7f, 0x80 and 0xff, and a random arg1 that the C
//! never reads. The callee stub returns 1, 0, -1 and 2 in turn, so the `== 1` test sees both
//! outcomes. Classes split the inputs by the sign bit of arg0[1], which the signed-read mutation needs.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").script_clearMusicLoadSlot));

pub const classes = [_][:0]const u8{ "byte-positive", "byte-negative" };

pub const stubs = struct {
    pub fn vs_main_clearMusicLoadSlot(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        const r = ctx.value(u8);
        return if (r < 100) 1 else if (r < 150) 0 else if (r < 200) -1 else 2;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(4, 1);
    buf[1] = if (ctx.chance(2))
        ctx.pick(u8, &.{ 0, 1, 2, 0x7f, 0x80, 0xff })
    else
        ctx.int(u8);
    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const b = args[0][1];
    return if (b & 0x80 != 0) 1 else 0;
}
