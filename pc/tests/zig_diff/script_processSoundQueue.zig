//! zig_diff spec: vs_battle_script_processSoundQueue -> script_process_sound_queue.script_processSoundQueue (4A0A8.c).
//! The C ignores both arguments and returns whether vs_main_processSoundQueue() is exactly 1. The
//! inputs vary arg0 (null or a valid arena byte) and arg1 (edge shorts). The callee stub returns 1
//! about a quarter of the time, -1 about a quarter, and otherwise any c_int, so the `== 1` test sees
//! both outcomes and values above and below 1.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").script_processSoundQueue));

pub const classes = [_][:0]const u8{ "arg0-null", "arg0-set" };

pub const stubs = struct {
    pub fn vs_main_processSoundQueue(ctx: *zd.Ctx) c_int {
        if (ctx.value(u2) == 0) return 1;
        if (ctx.value(u2) == 0) return -1;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const a1 = ctx.pick(c_short, &.{ 0, 1, -1, 32767, -32768 });
    if (ctx.chance(4)) return .{ null, a1 };
    const p: [*c]u8 = ctx.allocBytes(1, 1);
    return .{ p, a1 };
}

pub fn classify(args: Args) usize {
    return if (args[0] == null) 0 else 1;
}
