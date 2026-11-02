//! zig_diff spec: vs_battle_script_dismissTextBox -> script_dismiss_text_box.script_dismissTextBox (4A0A8.c).
//! Inputs: arg0 is a 4-byte arena buffer filled with random bytes, so the byte
//! the C passes on (arg0[1]) varies from run to run, and arg1 is any short
//! (edge values included). The C does not use arg1 but always returns 0, so
//! the classes split on arg1 being zero or not. The dismiss callee is a
//! recording stub; it returns a value the C discards, and the call's argument
//! is recorded, so the byte's value is compared exactly.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").script_dismissTextBox));

pub const classes = [_][:0]const u8{ "arg1-zero", "arg1-nonzero" };

pub const stubs = struct {
    pub fn vs_battle_dismissTextBox(ctx: *zd.Ctx, arg: anytype) c_int {
        _ = arg;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf: [*c]u8 = @ptrCast(ctx.allocBytes(4, 1));
    return .{ buf, ctx.pick(c_short, &.{ 0, -1, 1, 0x7FFF, -0x8000, 0x100, 0x80 }) };
}

pub fn classify(args: Args) usize {
    return if (args[1] == 0) 0 else 1;
}
