//! zig_diff spec: vs_battle_script_loadMusicSlot -> script_load_music_slot.script_loadMusicSlot (4A0A8.c).
//! Inputs: a small arena block as arg0 (never null, since the C reads arg0[1] and arg0[2]),
//! with both id bytes drawn from edge values (0, 1, 127, 128, 255) or any byte, and a random
//! arg1 (the C never reads it). The C has no branches and no comparisons. Its one callee,
//! vs_main_loadMusicSlot, is a recording stub returning ctx.value(c_int); the C ignores that
//! result and writes no state the C itself reads back, so the default stub models it.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").script_loadMusicSlot));

pub const classes = [_][:0]const u8{ "low-id", "high-id" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.allocBytes(8, 1);
    block[1] = ctx.pick(u8, &.{ 0, 1, 127, 128, 255 });
    block[2] = ctx.pick(u8, &.{ 0, 1, 127, 128, 255 });
    return .{ block, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    return if (args[0][1] >= 128) 1 else 0;
}
