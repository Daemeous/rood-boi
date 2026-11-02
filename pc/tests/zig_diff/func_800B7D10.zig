//! zig_diff spec: func_800B7D10 -> func_800B7D10.func_800B7D10 (4A0A8.c).
//! Inputs: the mode byte D_800F4C2C at 2 about a quarter of the time (the
//! 0x8000 pad path) and other values otherwise; arg0 an arena block (only
//! the callees read it); arg1 any i16. The callee func_800BFE50 returns an
//! index below 24, the size of D_800F4B70, a third of the time with bits
//! above 16 set that the C's u_short drops. The vector stubs spill the
//! SVECTOR they are given to the scratchpad (func_8007CFF8's spill leaves out
//! pad: the C leaves it unset there and the callee reads only vx and vz),
//! and func_8009DC8C sometimes
//! overwrites a D_800F4B70 entry, so a store moved across that call shows.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B7D10));

pub const classes = [_][:0]const u8{ "mode-is-2", "mode-not-2" };

/// Copies the first `n` i16 fields of an SVECTOR to the scratchpad at `off`.
fn spill(off: usize, p: anytype, comptime n: usize) void {
    const src: *const [n]i16 = @ptrCast(@alignCast(p));
    const dst: *[n]i16 = @ptrFromInt(zd.scratchpad + off);
    dst.* = src.*;
}

pub const stubs = struct {
    pub fn func_800BFE50(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        const actor: c_int = ctx.value(u8) % 24;
        return if (ctx.value(u8) % 3 == 0) actor | (@as(c_int, ctx.value(u8)) << 16) else actor;
    }

    pub fn func_800A9D90(ctx: *zd.Ctx, a: anytype, p: anytype, b: anytype) void {
        _ = ctx;
        _ = a;
        _ = b;
        spill(0xF00, p, 4);
    }

    pub fn func_8007CFF8(ctx: *zd.Ctx, a: anytype, p: anytype) void {
        _ = ctx;
        _ = a;
        spill(0xF10, p, 3);
    }

    pub fn func_8009DC8C(ctx: *zd.Ctx, x: anytype) void {
        if (ctx.value(u8) < 64) {
            const idx: usize = @intCast(x);
            v.g.D_800F4B70()[idx] = ctx.value(u8);
        }
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    if (ctx.chance(4)) {
        v.g.D_800F4C2C().* = 2;
    } else {
        v.g.D_800F4C2C().* = ctx.pick(u8, &.{ 0, 1, 3, 255 });
    }
    const arg0: [*c]u8 = ctx.allocBytes(8, 4);
    const arg1 = ctx.int(i16);
    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (v.g.D_800F4C2C().* == 2) 0 else 1;
}
