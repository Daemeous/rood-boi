//! zig_diff spec: func_800BA94C -> func_800BA94C.func_800BA94C (4A0A8.c).
//! Inputs: a 4-byte arena buffer whose byte 1 is 0 or 5 (often) or another
//! value (including 0x85 and 0xFF, which a signed-char read misreads), the
//! gate D_800F4C2C at 2 (about 3 in 4) or another byte, the flag D_800F4BB9
//! zero, 1, 0x80 or 0xFF, and an arg1 that the C never reads. The stub
//! func_8007BEE4 returns the byte's own value about half the time (a match)
//! and any i32 otherwise, and may write the flag; func_800CF1A8 may write the
//! gate. Classes: gate closed, byte not 0/5, flag clear, flag set.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BA94C));

pub const classes = [_][:0]const u8{ "gate-closed", "not-0-or-5", "flag-clear", "flag-set" };

/// The byte prepare stored at arg0[1], so the stub can return a match.
var want: u8 = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    const p: [*]u8 = ctx.allocBytes(4, 4);
    const b: u8 = if (ctx.chance(2)) ctx.edge(u8, &.{ 0, 5 }) else ctx.pick(u8, &.{ 1, 4, 6, 0x85, 0xFF });
    p[1] = b;
    want = b;
    const gate = v.g.D_800F4C2C();
    gate.* = if (ctx.chance(4)) ctx.edge(u8, &.{ 0, 1, 3, 0x82, 0xFF }) else 2;
    const flag = v.g.D_800F4BB9();
    flag.* = ctx.pick(u8, &.{ 0, 1, 0x80, 0xFF });
    return .{ p, ctx.pick(c_short, &.{ 0, -1, 1, 32767, -32768 }) };
}

pub fn classify(args: Args) usize {
    const p = args[0];
    const b = p[1];
    if (v.g.D_800F4C2C().* != 2) return 0;
    if (b != 0 and b != 5) return 1;
    if (v.g.D_800F4BB9().* == 0) return 2;
    return 3;
}

pub const stubs = struct {
    pub fn func_8007BEE4(ctx: *zd.Ctx) c_int {
        if (ctx.value(u1) == 1) v.g.D_800F4BB9().* = ctx.value(u8);
        if (ctx.value(u1) == 1) return @as(c_int, want);
        return ctx.value(i32);
    }
    pub fn func_800CF1A8(ctx: *zd.Ctx) void {
        if (ctx.value(u1) == 1) v.g.D_800F4C2C().* = ctx.value(u8);
    }
};
