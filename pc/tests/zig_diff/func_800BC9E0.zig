//! zig_diff spec: func_800BC9E0 -> func_800BC9E0.func_800BC9E0 (4A0A8.c).
//! The C is straight-line: no branches and no comparisons, so the coverage and
//! value-edge rules have nothing to hit. The inputs cover the state the copies
//! read: the camera block the global points at (random bytes, or all zero in
//! the "zeroed-state" class), and a second arena block that the stubs may
//! retarget the global to. Retargeting makes the port re-read
//! D_800F4BA4 after each call, as the C does. The stubs write argument fields
//! and the global, so every call's state hash and the final state depend on
//! the order of the stores and calls.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BC9E0));

pub const classes = [_][:0]const u8{ "random-state", "zeroed-state" };

var other: ?*v.D_800F4BA4_t = null;
var zeroed: bool = false;

pub fn prepare(ctx: *zd.Ctx) Args {
    const cur = ctx.alloc(v.D_800F4BA4_t);
    other = ctx.alloc(v.D_800F4BA4_t);
    zeroed = ctx.chance(4);
    if (zeroed) @memset(std.mem.asBytes(cur), 0);
    v.g.D_800F4BA4().* = cur;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (zeroed) 1 else 0;
}

pub const stubs = struct {
    pub fn func_800BCA8C(ctx: *zd.Ctx, a: anytype, b: anytype) void {
        _ = b;
        const s: *v.D_800F4BA4_t2 = @ptrCast(@alignCast(a));
        if (ctx.value(u1) == 1) s.target.vy = ctx.value(i32);
        if (ctx.value(u1) == 1) s.target.vz = ctx.value(i32);
        if (ctx.value(u3) == 0) v.g.D_800F4BA4().* = other.?;
    }

    pub fn func_800BD57C(ctx: *zd.Ctx, p: anytype) void {
        const s: *v.func_800BD57C_t = @ptrCast(@alignCast(p));
        if (ctx.value(u1) == 1) s.unk2 = ctx.value(i16);
    }

    pub fn func_800BDF6C(ctx: *zd.Ctx, p: anytype) void {
        const s: *v.func_800BDF6C_unk180_t = @ptrCast(@alignCast(p));
        if (ctx.value(u1) == 1) s.unk8 = ctx.value(u8);
    }

    pub fn func_800BE180(ctx: *zd.Ctx) void {
        if (ctx.value(u1) == 1) v.g.D_800F4BA4().*.?.unk1BC = ctx.value(i16);
    }
};
