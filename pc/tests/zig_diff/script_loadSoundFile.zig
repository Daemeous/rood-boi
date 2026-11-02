//! zig_diff spec: vs_battle_script_loadSoundFile -> script_load_sound_file.script_loadSoundFile (4A0A8.c).
//! The C has no branches and no comparisons. Its only data flow is the id
//! that func_80089104 returns, narrowed to short and passed on to
//! vs_main_loadMusicSlot (slot 3) and vs_main_loadSoundFile. The stub returns
//! full-width c_int values (high bits set, negative values, values outside the
//! short range), so a port that widens or narrows the id differently shows in
//! the traced arguments. arg0 is sometimes null and sometimes a live arena
//! pointer; the C never reads it. arg1 is edge-biased (negative, zero,
//! positive, and the short extremes) so every input class occurs. The callees'
//! return values are ignored by the C, so the default stubs suffice.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").script_loadSoundFile));

pub const classes = [_][:0]const u8{ "arg1-negative", "arg1-zero", "arg1-positive" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(16, 16);
    const arg0: [*c]u8 = if (ctx.chance(4)) null else @ptrCast(buf);
    const arg1 = ctx.pick(c_short, &.{ 0, -1, 1, 32767, -32768 });
    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    if (args[1] < 0) return 0;
    if (args[1] == 0) return 1;
    return 2;
}
