//! zig_diff spec: pc_battle_event_arguments -> scene_submit.battleEventArguments (146C.c, scene_submit.inc).
//! Inputs: the event runner's state (4 mostly), the thread (0..3 and the edges around it, the extremes of
//! a 32-bit int), the thread's script pointer (none or a buffer of random bytes), its opcode (the text box
//! 0x11, the nine actor-reference handlers, their neighbours, others) with the interpreter's operand size
//! D_800E9C30 for it (0 and 1 below the count's range, 2 to 17, 18 and above), the actor reference's high
//! nibble around 2 and what func_800BFE50 gives back (in and out of 0..16, depending on its argument).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").battleEventArguments));

pub const classes = [_][:0]const u8{ "not-running", "bad-thread", "no-script", "bad-count", "text-box", "actor-op", "plain-op" };

/// What func_800BFE50 returns: its argument folded into -2..17, an edge, or any value.
var kind: usize = 0;

pub const stubs = struct {
    pub fn func_800BFE50(ctx: *zd.Ctx, x: anytype) c_int {
        return switch (kind) {
            0 => @as(c_int, @intCast(@as(u16, x) % 20)) - 2,
            1 => ctx.edge(c_int, &.{ -1, 0, 16, 17, 0x100, std.math.minInt(c_int) }),
            else => ctx.value(c_int),
        };
    }
};

const ops = [_]u8{ 0x11, 0x11, 0x20, 0x21, 0x22, 0x2b, 0x2c, 0x33, 0x34, 0x35, 0x36, 0x1F, 0x23, 0x2a, 0x2d, 0x32, 0x37, 0x10, 0x12, 0x00, 0xFF };

pub fn prepare(ctx: *zd.Ctx) Args {
    kind = ctx.random.uintLessThan(usize, 3);
    v.g.D_800F4C34().* = ctx.edge(u8, &.{ 4, 4, 4, 4, 4, 4, 0, 3, 5 });
    const thread = ctx.edge(c_int, &.{ 0, 1, 2, 3, 0, 1, 2, 3, -1, 4, std.math.maxInt(c_int), std.math.minInt(c_int) });
    const script = ctx.allocBytes(32, 1);
    const op = ctx.edge(u8, &ops);
    script[0] = op;
    script[2] = ctx.edge(u8, &.{ 0x00, 0x0F, 0x10, 0x1F, 0x20, 0x21, 0xFF, ctx.int(u8) });
    for (0..4) |i| v.g.D_800F4C38()[i] = if (ctx.chance(8)) null else @ptrCast(script);
    v.g.D_800E9C30()[op] = ctx.edge(u8, &.{ 0, 1, 2, 3, 4, 5, 9, 17, 18, 19, 255 });
    return .{ thread, @ptrCast(ctx.alloc([20]c_int)), ctx.alloc(c_int), ctx.alloc(c_int) };
}

pub fn classify(args: Args) usize {
    const thread = args[0];
    if (v.g.D_800F4C34().* != 4) return 0;
    if (thread < 0 or thread >= 4) return 1;
    const pc: [*]u8 = @ptrCast(v.g.D_800F4C38()[@intCast(thread)] orelse return 2);
    const n = @as(c_int, v.g.D_800E9C30()[pc[0]]) - 1;
    if (n < 0 or n > 16) return 3;
    if (pc[0] == 0x11) return 4;
    return switch (pc[0]) {
        0x20, 0x21, 0x22, 0x2b, 0x2c, 0x33, 0x34, 0x35, 0x36 => 5,
        else => 6,
    };
}
