//! zig_diff spec: func_800E6898 -> tile_occupancy.addTileOccupancy (D2: pc/src/battle_movement.c, through its static tile_occupancy).
//! Inputs: an actor of random bytes whose placement word (0x5C) has its x in
//! bits 0-7 (0..31), bits 8-15 random and z in bits 16-23 (0..31), the top
//! byte random (the C shifts it out); D_800F58B8 null a fifth of the time,
//! else nonzero; D_800F58D0 the address of a 4 KB grid of 16-bit cells of
//! random bytes, so the count wraps through 0xFC00 / 0x0400 as the cell falls.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_movement");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").addTileOccupancy));

pub const classes = [_][:0]const u8{ "no-grid", "counted" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.allocBytes(0x100, 16);
    for (actor[0..0x100]) |*byte| byte.* = ctx.int(u8);
    const placement: u32 = ctx.range(u32, 0, 31) | (@as(u32, ctx.int(u8)) << 8) | (ctx.range(u32, 0, 31) << 16) | (@as(u32, ctx.int(u8)) << 24);
    @as(*align(1) u32, @ptrCast(actor + 0x5c)).* = placement;
    const grid = ctx.allocBytes(0x1000, 16);
    for (grid[0..0x1000]) |*byte| byte.* = ctx.int(u8);
    v.g.D_800F58D0().* = @intCast(@intFromPtr(grid));
    v.g.D_800F58B8().* = if (ctx.chance(5)) 0 else @intCast(@intFromPtr(grid));
    return .{actor};
}

pub fn classify(args: Args) usize {
    _ = args;
    return @intFromBool(v.g.D_800F58B8().* != 0);
}
