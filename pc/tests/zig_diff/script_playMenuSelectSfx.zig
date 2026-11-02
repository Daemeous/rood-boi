//! zig_diff spec: vs_battle_script_playMenuSelectSfx -> script_play_menu_select_sfx.script_playMenuSelectSfx (4A0A8.c).
//! The C has no branches and reads no state: it calls vs_main_playSfxDefault(0x7E, 5) and returns 0.
//! The inputs cover a null and a non-null arg0 (the C never dereferences it), and arg1 across
//! zero, its sign and the short extremes (the C ignores it, so a port that returns or uses it mismatches).
//! The recording stub for the sfx call captures its arguments in both runs.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").script_playMenuSelectSfx));

pub const classes = [_][:0]const u8{ "null-arg0", "arg0-arg1-nonneg", "arg0-arg1-neg" };

pub fn prepare(ctx: *zd.Ctx) Args {
    var arg0: [*c]u8 = null;
    if (!ctx.chance(4)) arg0 = ctx.allocBytes(16, 16);
    const arg1 = ctx.pick(c_short, &.{ 0, 1, -1, 0x7FFF, -32768 });
    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    if (args[0] == null) return 0;
    return if (args[1] < 0) 2 else 1;
}
