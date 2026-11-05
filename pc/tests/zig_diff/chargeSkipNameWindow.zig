//! zig_diff spec: pc_charge_skip_name_window -> charge_skip.nameWindow (146C.c, charge_skip.inc).
//! Inputs: the name window text allocated or not, text box 7's state (-1, 0, 1, 5), the held
//! flag, the byte count asked for (0, 1, 111, 112, 113, 200, negative); the output block is 128 bytes.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").nameWindow));

pub const classes = [_][:0]const u8{ "closed", "copied-short", "copied-112" };

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g.nameWindowText().* = if (ctx.chance(5)) null else @ptrCast(@alignCast(ctx.allocBytes(128, 16)));
    v.g.textBoxes()[7].state = ctx.edge(@TypeOf(v.g.textBoxes()[7].state), &.{ -1, 0, 1, 5, 1, 1 });
    v.g.charge_skip_held().* = ctx.edge(c_int, &.{ 0, 1, 9 });
    const out = ctx.allocBytes(128, 16);
    return .{ out, ctx.edge(c_int, &.{ 0, 1, 5, 111, 112, 113, 200, -1, std.math.minInt(c_int) }), ctx.alloc(c_int) };
}

pub fn classify(args: Args) usize {
    if (v.g.nameWindowText().* == null or v.g.textBoxes()[7].state <= 0) return 0;
    return if (args[1] < 112) 1 else 2;
}
