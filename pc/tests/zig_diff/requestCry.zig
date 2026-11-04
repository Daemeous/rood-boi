//! zig_diff spec: func_800E4180 -> pc.cry_request.requestCry (D2: pc/src/battle_movement.c).
//! Inputs: a state block with the request entry of a slot (0..31) at 0x474
//! holding a running count (0, 1, 2 and the pitch thresholds 0xB, 0x12, 0x35,
//! 0xFF) and a stored kind byte; a requested kind (0, 1, 2, 3, others) whose
//! length D_800F1758[kind] is one of the thresholds; the chance and the roll
//! the func_800E45D4 stub returns (equal, one off, any), so the new-request
//! gate is hit at its boundary. kind stays 0..40: the unsized table D_800F1758
//! is sized by the harness.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_movement");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").requestCry));

pub const classes = [_][:0]const u8{ "running-voice", "running-other", "new-kind-3", "new-request" };

var roll: c_int = 0;
const thresholds = [_]u8{ 0, 1, 2, 3, 0xA, 0xB, 0xC, 0x11, 0x12, 0x13, 0x34, 0x35, 0x36, 0xFF };

pub const stubs = struct {
    pub fn func_800E45D4(ctx: *zd.Ctx, n: anytype) c_int {
        _ = ctx;
        _ = n;
        return roll;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.allocBytes(0x500, 16);
    for (block[0..0x500]) |*byte| byte.* = ctx.int(u8);
    const slot = ctx.range(c_int, 0, 31);
    const kind = ctx.edge(c_int, &.{ 1, 1, 2, 2, 3, 0, 4, 40 });
    const entry: *align(1) u16 = @ptrCast(block + 0x474 + @as(usize, @intCast(slot)) * 2);
    const count: u8 = if (ctx.chance(3)) 0 else ctx.edge(u8, &thresholds);
    entry.* = (@as(u16, count) << 8) | ctx.pick(u8, &.{ 0, 1, 2, 3 });
    v.g.D_800F1758()[@intCast(kind)] = ctx.edge(u8, &thresholds);
    const chance = ctx.pick(c_int, &.{ 0, 1, 0x7F, 0x80, -1, 0x7FFFFFFF });
    roll = switch (ctx.range(u8, 0, 3)) {
        0 => chance,
        1 => chance +% 1,
        2 => chance -% 1,
        else => ctx.pick(c_int, &.{ 0, 0x7F }),
    };
    return .{ block, chance, kind, slot };
}

pub fn classify(args: Args) usize {
    const entry: *align(1) const u16 = @ptrCast(args[0] + 0x474 + @as(usize, @intCast(args[3])) * 2);
    if ((entry.* >> 8) != 0) return if (args[2] == 1 or args[2] == 2) 0 else 1;
    return if (args[2] == 3) 2 else 3;
}
