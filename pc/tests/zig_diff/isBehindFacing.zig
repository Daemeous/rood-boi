//! zig_diff spec: func_800E47E4 -> pc.behind_facing.isBehindFacing (D2: pc/src/battle_movement.c).
//! Inputs: a state block with the busy byte (0xC) clear or set, a facing
//! target (0x15C) among the 16 actor models, 15, 16 and 0xFF (16 and above use
//! the actor's own model, byte 0x88), the tile and the current tile (0x34)
//! random bytes or equal; positions in the models and the actor. The models
//! are 0x80-byte blocks in D_800F4538. The stubs of ratan2, rcos and rsin
//! return small values (also 0 and +-4096), so the two products and their
//! sum (equal to zero when the tiles agree) are told apart.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_movement");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").isBehindFacing));

pub const classes = [_][:0]const u8{ "busy", "own-heading", "target-model" };

fn small(ctx: *zd.Ctx) c_int {
    return switch (ctx.value(u3)) {
        0 => 0,
        1 => 4096,
        2 => -4096,
        3 => 1,
        4 => -1,
        else => @as(c_int, ctx.value(i16)) >> 3,
    };
}

pub const stubs = struct {
    pub fn ratan2(ctx: *zd.Ctx, a: anytype, b: anytype) c_int {
        _ = a;
        _ = b;
        return ctx.value(c_int);
    }
    pub fn rcos(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        return small(ctx);
    }
    pub fn rsin(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        return small(ctx);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.allocBytes(0x200, 16);
    for (block[0..0x200]) |*byte| byte.* = ctx.int(u8);
    for (0..16) |i| {
        const model = ctx.allocBytes(0x80, 16);
        for (model[0..0x80]) |*byte| byte.* = ctx.int(u8);
        v.g.D_800F4538()[i] = @ptrCast(model);
    }
    block[0xc] = ctx.edge(u8, &.{ 0, 0, 0, 1, 0x80 });
    block[0x15c] = ctx.edge(u8, &.{ 0, 5, 15, 15, 16, 16, 0xFF });
    block[0x88] = ctx.range(u8, 0, 15);
    const current: *align(1) u32 = @ptrCast(block + 0x34);
    var tile = ctx.int(u32) & 0x00FF00FF;
    if (ctx.chance(4)) tile = current.* & 0x00FF00FF;
    return .{ block, tile | (ctx.int(u32) & 0xFF00FF00) };
}

pub fn classify(args: Args) usize {
    if (args[0][0xc] != 0) return 0;
    return if (args[0][0x15c] >= 16) 1 else 2;
}
