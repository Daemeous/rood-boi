//! zig_diff spec: func_800E5EC0 -> func_800E5EC0.func_800E5EC0 (7CD68.c).
//! Inputs: a 32x32 flag grid (D_800F58B8) and a data grid (D_800F58D0) in the
//! arena, both 33 rows so a neighbour one past the edge stays in memory. The
//! start cell is any column and row 0..31, with 31 drawn half the time so the
//! `x < 32` and `z < 32` tests see their constant side equal (a neighbour at
//! 32 comes from dx or dz = 1). Every D_800F16EC step gets a random dx and dz
//! in -2..1, so negative and wrapped neighbours occur. arg2 is any c_int. The
//! flag and data cells keep their random bytes, so both the visited and the
//! unvisited paths run. The C calls only itself, so there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_7CD68");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E5EC0));

pub const classes = [_][:0]const u8{ "edge", "visited", "fresh" };

fn grid(ctx: *zd.Ctx) [*][32]u16 {
    return @ptrCast(@alignCast(ctx.allocBytes(33 * 32 * 2, 16)));
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const flags = grid(ctx);
    const cells = grid(ctx);
    v.g.D_800F58B8().* = flags;
    v.g.D_800F58D0().* = cells;

    const tbl = v.g.D_800F16EC();
    for (tbl) |*e| {
        psx.setBits(e, "dx", ctx.range(i2, -2, 1));
        psx.setBits(e, "dz", ctx.range(i2, -2, 1));
    }

    const arg0: c_int = if (ctx.chance(2)) 31 else ctx.range(i32, 0, 31);
    const arg1: c_int = if (ctx.chance(2)) 31 else ctx.range(i32, 0, 31);
    const arg2: c_int = ctx.int(c_int);
    return .{ arg0, arg1, arg2 };
}

pub fn classify(args: Args) usize {
    if (args[0] == 31 or args[1] == 31) return 0;
    const cells = v.g.D_800F58D0().*.?;
    const cell = cells[@intCast(args[1])][@intCast(args[0])];
    return if ((cell & 0x2000) != 0) 1 else 2;
}
