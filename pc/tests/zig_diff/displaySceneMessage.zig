//! zig_diff spec: vs_battle_displaySceneMessage ->
//! scene_message.displaySceneMessage (5BF94.c).
//! Inputs: message 11 (an action's name), 12 (spell mastered) and others
//! (a scene message, including a few just outside the table, as the C
//! allows); action 0, 1-0x1C9 (a row before D_800EBF58), 0x1CA and up, and
//! past the 256 actions (only its address is taken outside message 11);
//! held 0, 1, 256 (narrows to 0) or random. The heap allocation is an arena
//! block; the text calls and both ROOD_PC hooks are recording stubs.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").displaySceneMessage));

pub const classes = [_][:0]const u8{ "message-11", "message-12", "other-action-0", "other-action" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const message: c_int = switch (ctx.range(u8, 0, 3)) {
        0 => 11,
        1 => 12,
        else => ctx.range(c_int, -8, 200),
    };
    const action: c_int = switch (ctx.range(u8, 0, 4)) {
        0 => 0,
        1 => ctx.range(c_int, 1, 0x1C9),
        2 => ctx.range(c_int, 0x1CA, 0x1FF),
        else => if (message == 11) ctx.range(c_int, 0, 255) else ctx.range(c_int, 0, 1000),
    };
    const held: c_int = ctx.pick(c_int, &.{ 0, 1, 256, 255 });
    return .{ message, if (message == 11) @mod(action, 256) else action, held };
}

pub fn classify(args: Args) usize {
    if (args[0] == 11) return 0;
    if (args[0] == 12) return 1;
    return if (args[1] == 0) 2 else 3;
}

pub const stubs = struct {
    pub fn vs_main_allocHeapR(ctx: *zd.Ctx, size: c_uint) ?*anyopaque {
        return ctx.allocBytes(size, 16);
    }
};
