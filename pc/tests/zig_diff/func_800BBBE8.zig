//! zig_diff spec: func_800BBBE8 -> func_800BBBE8.func_800BBBE8 (4A0A8.c).
//! The function takes no arguments and its body is straight-line, so there is one
//! input class. The inputs are the random camera vectors the stubs return for
//! getCameraPosition and getCameraLookAt, and the random bytes of the arena and
//! the globals. The stubs model the callees: the allocator hands out an arena
//! block the C fills, and the void callees write fields of that block and of the
//! globals they are passed. One callee reads a field the C stores just before
//! it, so a port that moves a store across a call changes the result.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BBBE8));

pub const classes = [_][:0]const u8{"camera"};

pub fn prepare(ctx: *zd.Ctx) Args {
    _ = ctx;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}

fn camera() *v.D_800F4BA4_t {
    return v.g.D_800F4BA4().*.?;
}

pub const stubs = struct {
    pub fn vs_main_allocHeapR(ctx: *zd.Ctx, size: anytype) ?*anyopaque {
        _ = size;
        return @ptrCast(ctx.allocBytes(@sizeOf(v.D_800F4BA4_t), 16));
    }

    pub fn vs_battle_getCameraPosition(ctx: *zd.Ctx, out: anytype) void {
        const o: *v.VECTOR = @ptrCast(@alignCast(out));
        o.* = .{
            .vx = ctx.value(i32),
            .vy = ctx.value(i32),
            .vz = ctx.value(i32),
            .pad = ctx.value(i32),
        };
    }

    pub fn vs_battle_getCameraLookAt(ctx: *zd.Ctx, out: anytype) void {
        const o: *v.VECTOR = @ptrCast(@alignCast(out));
        o.* = .{
            .vx = ctx.value(i32),
            .vy = ctx.value(i32),
            .vz = ctx.value(i32),
            .pad = ctx.value(i32),
        };
    }

    pub fn func_8007DD50(ctx: *zd.Ctx, arg: anytype) void {
        _ = arg;
        const d = camera();
        d.eye.follow = ctx.value(u8);
    }

    pub fn func_8007DDAC(ctx: *zd.Ctx, arg: anytype) void {
        _ = ctx;
        _ = arg;
        // Reads the flag the C stores after this call: a port that stores it
        // before the call makes the stub take the other branch.
        const d = camera();
        d.unk1BC = if (d.unk174.unk0 == 1) 1 else 2;
    }

    pub fn func_8007DDB8(ctx: *zd.Ctx, p: anytype) void {
        const d: *v.D_800F1A68_t = @ptrCast(@alignCast(p));
        d.unk4 = ctx.value(i32);
    }

    pub fn func_8007DDD4(ctx: *zd.Ctx, p: anytype) void {
        const d: *v.P_CODE = @ptrCast(@alignCast(p));
        d.r0 = ctx.value(u8);
    }

    pub fn func_8007DDF8(ctx: *zd.Ctx, p: anytype) void {
        const d: *v.D_800F1A68_t = @ptrCast(@alignCast(p));
        d.unk8 = ctx.value(i32);
    }

    pub fn func_8007DE2C(ctx: *zd.Ctx, arg: anytype) void {
        _ = arg;
        const d = camera();
        d.unk1EC.vx = ctx.value(i32);
    }
};
