//! zig_diff spec: func_800E4904 -> pc.approach_point.refreshApproachPoint (D2: pc/src/battle_movement.c).
//! Inputs: a state block with the target actor id (0xE5, 0..15), models for
//! all 16 slots of D_800F4538 (0x80-byte blocks: flag bits at 0xA, the
//! waypoint byte 0x5D at 0, 1, 2, 5 or 0xFF, the flagged group word at 0x60
//! with byte 1 at 0, 2, 3 or 0xFF, never 1 (that leaves the position
//! uninitialised in the original), and short positions at 0x1C/0x20/0x4C/0x50),
//! and the scratchpad table at 0x37C (word per actor) zero or not. The stubs:
//! func_8008E2D4 returns an arena waypoint record, func_800DFA54 records its
//! arguments (the base+0xF4 pointer included); the shifted bytes at 0xF4/0xF6
//! are then the state's own.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_movement");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").refreshApproachPoint));

pub const classes = [_][:0]const u8{ "inactive", "plain-position", "plain-group", "flagged-position", "flagged-group" };

var waypoints: [*c]c_uint = null;

pub const stubs = struct {
    pub fn func_8008E2D4(ctx: *zd.Ctx, group: anytype) [*c]c_uint {
        _ = ctx;
        _ = group;
        return waypoints;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.allocBytes(0x200, 16);
    for (block[0..0x200]) |*byte| byte.* = ctx.int(u8);
    waypoints = @ptrCast(@alignCast(ctx.allocBytes(16, 16)));
    for (waypoints[0..4]) |*w| w.* = ctx.int(c_uint);
    for (0..16) |i| {
        const model = ctx.allocBytes(0x80, 16);
        for (model[0..0x80]) |*byte| byte.* = ctx.int(u8);
        if (ctx.chance(2)) model[0xa] = ctx.edge(u8, &.{ 0, 0xE0, 1 });
        model[0x5d] = ctx.edge(u8, &.{ 0, 1, 2, 5, 0xFF });
        model[0x61] = ctx.edge(u8, &.{ 0, 2, 3, 0xFF });
        v.g.D_800F4538()[i] = @ptrCast(model);
    }
    block[0xe5] = ctx.range(u8, 0, 15);
    const present: [*]u32 = @ptrFromInt(0x1F80037C);
    for (present[0..16]) |*word| {
        if (ctx.chance(4)) word.* = 0;
    }
    return .{block};
}

pub fn classify(args: Args) usize {
    const id = args[0][0xe5];
    const present: [*]const u32 = @ptrFromInt(0x1F80037C);
    if (present[id] == 0) return 0;
    const model: [*]const u8 = @ptrCast(v.g.D_800F4538()[id].?);
    if ((model[0xa] & 0x1f) == 0) return if (model[0x5d] < 2) 1 else 2;
    return if (model[0x61] == 0) 3 else 4;
}
