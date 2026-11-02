//! zig_diff spec: func_800B75F4 -> func_800B75F4.func_800B75F4 (4A0A8.c).
//! Inputs: an 8-byte opcode (bytes 3 and 5, the cell, sometimes 0 or 255),
//! arg1 (unused) any i16, and D_800F4C2C 2 (the func_800A9D90 branch, half
//! the runs) or any byte (the func_8007CFF8 branch). The stubs taking the
//! stack SVECTOR copy it into an arena block, so its fields are compared
//! (func_8007CFF8's copy leaves out pad: the C leaves it unset there and the
//! callee reads only vx and vz). func_800BFE50's actor is in 0..23 (the
//! D_800F4B70 store stays in its 24 bytes), a third of the time with bits
//! above 16 set that the C's u_short drops.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B75F4));

pub const classes = [_][:0]const u8{ "pad-path", "cff8-path" };

/// Arena block the recording stubs copy the SVECTORs they are given into.
var log: [*]u8 = undefined;

pub const stubs = struct {
    pub fn func_800BFE50(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        const actor: c_int = @intCast(ctx.value(u32) % 24);
        return if (ctx.value(u8) % 3 == 0) actor | (@as(c_int, ctx.value(u8)) << 16) else actor;
    }

    pub fn func_800A9D90(ctx: *zd.Ctx, a: anytype, p: anytype, b: anytype) void {
        _ = ctx;
        _ = a;
        _ = b;
        const src: [*]const u8 = @ptrCast(p);
        @memcpy(log[0..8], src[0..8]);
    }

    pub fn func_8007CFF8(ctx: *zd.Ctx, a: anytype, p: anytype) void {
        _ = ctx;
        _ = a;
        const src: [*]const u8 = @ptrCast(p);
        @memcpy(log[0..6], src[0..6]);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    log = ctx.allocBytes(8, 8);
    const bytes = ctx.allocBytes(8, 1);
    if (ctx.chance(4)) bytes[3] = ctx.edge(u8, &.{ 0, 255 });
    if (ctx.chance(4)) bytes[5] = ctx.edge(u8, &.{ 0, 255 });
    v.g.D_800F4C2C().* = if (ctx.chance(2)) 2 else ctx.int(u8);
    return .{ @as([*c]u8, @ptrCast(bytes)), ctx.int(i16) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (v.g.D_800F4C2C().* == 2) 0 else 1;
}
