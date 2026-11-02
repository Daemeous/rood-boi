//! zig_diff spec: func_800B7A98 -> func_800B7A98.func_800B7A98 (4A0A8.c).
//! Inputs: a 7-byte record in the arena (its bytes 3-5 random, byte 6 0xFF on
//! about a third of runs, otherwise any byte) and D_800F4C2C drawn from
//! {2, 0, 1, 255} or any byte. The callee results are masked so the table
//! index stays inside D_800F4B70's 24 entries: 0..23 covers every slot the
//! C can write without leaving the table. The three callees return values
//! only, and the C reads nothing back from them, so no stub writes state.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B7A98));

pub const classes = [_][:0]const u8{ "mode-2", "mode-else-ff", "mode-else-byte" };

pub const stubs = struct {
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = p;
        return ctx.value(c_short);
    }

    /// Returns a slot of D_800F4B70 (0..23): the C stores through it.
    pub fn func_800BFE50(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        return @as(c_int, ctx.value(u8) % 24);
    }

    pub fn func_800A9C54(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype) void {
        _ = ctx;
        _ = a;
        _ = b;
        _ = c;
    }

    /// Reads D_800F4B70 through the slot it is given: the state hash at the
    /// call shows whether the C's store landed before the call.
    pub fn func_8009DC8C(ctx: *zd.Ctx, x: anytype) void {
        _ = ctx;
        _ = x;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const p = ctx.allocBytes(8, 1);
    if (ctx.chance(3)) p[6] = 0xFF;
    v.g.D_800F4C2C().* = ctx.pick(u8, &.{ 2, 0, 1, 255 });
    return .{ p, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    if (v.g.D_800F4C2C().* == 2) return 0;
    if (args[0][6] == 0xFF) return 1;
    return 2;
}
