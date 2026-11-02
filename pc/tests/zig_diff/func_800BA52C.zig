//! zig_diff spec: func_800BA52C -> func_800BA52C.func_800BA52C (4A0A8.c).
//! Inputs: a 8-byte arena block as arg0, its bytes 1..4 random with some set to
//! edge bytes (0x7F, 0x80, 0xFF, 0) so the unsigned reads and the sign-extension
//! of the u_int argument are exercised, and the global D_800F4C69 sometimes equal
//! to arg0[1] (the store writes the same value) and sometimes not. The two callees
//! are recording stubs that sometimes rewrite arg0[1], arg0[3] and the global: the
//! C reads arg0[1..3] after the first call, so a port that reads them early differs.
//! The C has no branches or comparisons; the classes only steer which inputs occur.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BA52C));

pub const classes = [_][:0]const u8{ "high-byte-args", "plain-args", "global-equals-arg1" };

/// The argument block the stubs write into (set by prepare, same in both runs).
var buf: [*]u8 = undefined;

pub fn prepare(ctx: *zd.Ctx) Args {
    buf = ctx.allocBytes(8, 1);
    if (ctx.chance(3)) buf[ctx.range(usize, 1, 4)] = ctx.pick(u8, &.{ 0x7F, 0x80, 0xFF, 0 });
    if (ctx.chance(4)) v.g.D_800F4C69().* = buf[1];
    return .{buf};
}

pub fn classify(args: Args) usize {
    const a = args[0];
    if (v.g.D_800F4C69().* == a[1]) return 2;
    if (a[2] >= 0x80 or a[3] >= 0x80 or a[4] >= 0x80) return 0;
    return 1;
}

pub const stubs = struct {
    /// Called first: may rewrite arg0[1] (which the C re-reads) and the global.
    pub fn func_8004552C(ctx: *zd.Ctx, id: anytype, arg1: anytype, arg2: anytype) c_int {
        _ = id;
        _ = arg1;
        _ = arg2;
        if (ctx.value(u1) == 1) buf[1] = ctx.value(u8);
        if (ctx.value(u1) == 1) v.g.D_800F4C69().* = ctx.value(u8);
        return ctx.value(c_int);
    }
    /// Called second: may rewrite arg0[3] (not read after the call).
    pub fn func_80044F60(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype) c_int {
        _ = a;
        _ = b;
        _ = c;
        if (ctx.value(u1) == 1) buf[3] = ctx.value(u8);
        return ctx.value(c_int);
    }
};
