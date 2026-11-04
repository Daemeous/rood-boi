//! zig_diff spec: func_800E4C28 -> pc.clear_grid_cell_flag.clearGridCellFlag (D2: pc/src/actor_cleanup.c).
//! Inputs: D_800F58B8 zero a quarter of the time, else a random-filled cell
//! table inside a larger block (so cell offsets from x = -1 and z = -1, which
//! the unsigned arithmetic wraps, stay inside the arena); x and z from -1 to
//! 31 (the 32-wide rows). The target halfword is random, so bit 7 is set or
//! clear and the other bits must survive.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_actor_cleanup");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").clearGridCellFlag));

pub const classes = [_][:0]const u8{ "no-grid", "grid" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.allocBytes(0x1000, 16);
    for (block[0..0x1000]) |*byte| byte.* = ctx.int(u8);
    v.g.D_800F58B8().* = if (ctx.chance(4)) 0 else @intCast(@intFromPtr(block) + 0x80);
    return .{ ctx.range(c_int, -1, 31), ctx.range(c_int, -1, 31) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return @intFromBool(v.g.D_800F58B8().* != 0);
}
