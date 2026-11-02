//! zig_diff spec: func_800B784C -> func_800B784C.func_800B784C (4A0A8.c).
//! Inputs: a 16-byte command buffer in the arena. Bytes 1-2 feed
//! vs_battle_getShort (any short), bytes 3-6 are copied into the local
//! buffer, and bytes 7 and 8 are the 0xFF sentinels that map to -1; they
//! are drawn from 0xFF, 0xFE, 0 and 0x80 so both sides of each ternary
//! occur. arg1 is any short (the C never reads it). The game mode
//! D_800F4C2C is 2 in about a third of the runs and another byte otherwise.
//! func_800BFE50 returns an index in 0..23, the size of D_800F4B70, so the
//! C's store stays in bounds; a third of the time with bits above 16 set
//! that the C's u_short drops. func_800A97EC copies the 4 bytes it is given
//! into an arena log, so a wrong copy of the local buffer shows up in the
//! state. func_8009DC8C and the flag store are checked through the state
//! and the call trace.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B784C));

pub const classes = [_][:0]const u8{ "mode2", "other-byte7-ff", "other-byte8-ff", "other-plain" };

var log_buf: ?*[4]u8 = null;

pub const stubs = struct {
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = p;
        return ctx.value(c_short);
    }
    pub fn func_800BFE50(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        const actor: c_int = ctx.value(u8) % 24;
        return if (ctx.value(u8) % 3 == 0) actor | (@as(c_int, ctx.value(u8)) << 16) else actor;
    }
    pub fn func_800A97EC(ctx: *zd.Ctx, id: anytype, p: anytype, a: anytype, b: anytype) void {
        _ = ctx;
        _ = id;
        _ = a;
        _ = b;
        const src: [*]const u8 = @ptrCast(p);
        const dst = log_buf.?;
        for (0..4) |i| dst[i] = src[i];
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf: [*c]u8 = ctx.allocBytes(16, 16);
    buf[7] = ctx.pick(u8, &.{ 0xFF, 0xFE, 0, 0x80 });
    buf[8] = ctx.pick(u8, &.{ 0xFF, 0xFE, 0, 0x80 });
    log_buf = ctx.alloc([4]u8);
    const mode = ctx.pick(u8, &.{ 2, 2, 0, 1, 3, 0xFF });
    v.g.D_800F4C2C().* = mode;
    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    if (v.g.D_800F4C2C().* == 2) return 0;
    if (args[0][7] == 0xFF) return 1;
    if (args[0][8] == 0xFF) return 2;
    return 3;
}
