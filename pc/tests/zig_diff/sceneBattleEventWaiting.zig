//! zig_diff spec: pc_battle_event_waiting -> scene_submit.battleEventWaiting (146C.c, scene_submit.inc).
//! Inputs: the event runner's state (4 mostly), four thread pointers (none or a script of random bytes),
//! each script's opcode (0x15, 0x11, 0x59, neighbours, others), its operand (0..7, 8 and above, the
//! 0x20 bit set and clear), the text boxes' statuses (zero or not) and the end card flag D_800F4BF8.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").battleEventWaiting));

pub const classes = [_][:0]const u8{ "not-running", "no-threads", "buttons", "text-box", "end-card", "nothing" };

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g.D_800F4C34().* = ctx.edge(u8, &.{ 4, 4, 4, 4, 4, 0, 3, 5 });
    for (0..4) |i| {
        const script = ctx.allocBytes(4, 1);
        script[0] = ctx.edge(u8, &.{ 0x15, 0x15, 0x11, 0x11, 0x11, 0x59, 0x59, 0x14, 0x16, 0x10, 0x12, 0x58, 0x5A, 0x00, 0xFF });
        script[1] = ctx.edge(u8, &.{ 0, 1, 2, 7, 7, 8, 0x20, 0x21, 0x40, 0xFF, ctx.int(u8) });
        v.g.D_800F4C38()[i] = if (ctx.chance(4)) null else @ptrCast(script);
    }
    for (0..8) |k| v.g.vs_battle_textBoxStatuses()[k] = ctx.edge(u8, &.{ 0, 0, 1, 2, 0xFF });
    v.g.D_800F4BF8().* = ctx.edge(u8, &.{ 0, 0, 1, 2, 0x80, 0xFF });
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    if (v.g.D_800F4C34().* != 4) return 0;
    var any = false;
    var buttons = false;
    var box = false;
    var card = false;
    for (0..4) |i| {
        const pc: [*]u8 = @ptrCast(v.g.D_800F4C38()[i] orelse continue);
        any = true;
        if (pc[0] == 0x15) buttons = true else if (pc[0] == 0x11 and pc[1] < 8 and v.g.vs_battle_textBoxStatuses()[pc[1]] != 0) box = true else if (pc[0] == 0x59 and v.g.D_800F4BF8().* != 0) card = true;
    }
    if (!any) return 1;
    if (buttons) return 2;
    if (box) return 3;
    return if (card) 4 else 5;
}
