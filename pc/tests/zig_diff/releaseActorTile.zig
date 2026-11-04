//! zig_diff spec: func_800E678C -> pc.release_actor_tile.releaseActorTile (D2: pc/src/actor_cleanup.c).
//! Inputs: a grid record (D_800F58BC) with bounds bytes 1 (low x), 0 (high x),
//! 3 (low z) and 2 (high z) and a mask byte at 0xE (any value); an actor
//! state whose tile bytes (0x170, 0x172) are inside, just outside or exactly
//! on each bound, whose byte 0x19B is zero or nonzero, and whose byte 0x1A is
//! random (it must be cleared). The stub of func_800E4C28 records (x, z).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_actor_cleanup");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").releaseActorTile));

pub const classes = [_][:0]const u8{ "outside-idle", "inside-idle", "outside-active", "inside-active" };

fn near(ctx: *zd.Ctx, low: u8, high: u8) u8 {
    return switch (ctx.range(u8, 0, 5)) {
        0 => low -% 1,
        1 => low,
        2 => high,
        3 => high +% 1,
        4 => low +% (high -% low) / 2,
        else => ctx.int(u8),
    };
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const grid = ctx.allocBytes(0x40, 16);
    for (grid[0..0x40]) |*byte| byte.* = ctx.int(u8);
    v.g.D_800F58BC().* = @intCast(@intFromPtr(grid));
    grid[1] = ctx.range(u8, 0, 20);
    grid[0] = grid[1] + ctx.range(u8, 0, 10);
    grid[3] = ctx.range(u8, 0, 20);
    grid[2] = grid[3] + ctx.range(u8, 0, 10);
    const state = ctx.allocBytes(0x200, 16);
    for (state[0..0x200]) |*byte| byte.* = ctx.int(u8);
    state[0x170] = near(ctx, grid[1], grid[0]);
    state[0x172] = near(ctx, grid[3], grid[2]);
    state[0x19b] = ctx.edge(u8, &.{ 0, 0, 1, 0xFF });
    return .{state};
}

pub fn classify(args: Args) usize {
    const grid: [*]const u8 = @ptrFromInt(v.g.D_800F58BC().*);
    const s = args[0];
    const inside = s[0x170] >= grid[1] and s[0x170] <= grid[0] and s[0x172] >= grid[3] and s[0x172] <= grid[2];
    return @as(usize, @intFromBool(inside)) | @as(usize, @intFromBool(s[0x19b] != 0)) << 1;
}
