//! zig_diff spec: func_800C282C -> func_800C282C.func_800C282C (58578.c).
//! Inputs: no arguments. The camera-sync callee writes a yaw (either a random
//! i32 or one of the 16- and 32-bit edge values) and a random pitch, and may
//! repoint D_800EB9B8 at a second arena object. rcos and rsin return random
//! ints, so the stores into the scratchpad shorts wrap. D_800EB9B8 always
//! points at a valid, randomly filled arena object, so unk22 is compared with
//! the rest of the arena.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_58578");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C282C));

pub const classes = [_][:0]const u8{ "yaw-edge", "yaw-random", "yaw-random-global-moved" };

var edge_yaw: bool = false;
var yaw_edge: i32 = 0;
var swap_global: bool = false;
var obj_b: ?*v.D_800EB9B8_t = null;

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj_a = ctx.alloc(v.D_800EB9B8_t);
    obj_b = ctx.alloc(v.D_800EB9B8_t);
    v.g.D_800EB9B8().* = obj_a;
    edge_yaw = ctx.chance(3);
    yaw_edge = ctx.pick(i32, &.{ 0, -1, 1, 0x7FFF, 0x8000, 0xFFFF, 0x10000, std.math.maxInt(i32), std.math.minInt(i32) });
    swap_global = ctx.chance(3);
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    if (edge_yaw) return 0;
    return if (swap_global) 2 else 1;
}

pub const stubs = struct {
    pub fn vs_battle_syncCameraAnglesFromPosition(ctx: *zd.Ctx, cam: anytype) c_int {
        const cam_v: *v._sphericalCamera = @ptrCast(@alignCast(cam));
        cam_v.values.yaw = if (edge_yaw) yaw_edge else ctx.value(i32);
        cam_v.values.pitch = ctx.value(i32);
        if (swap_global) v.g.D_800EB9B8().* = obj_b;
        return ctx.value(c_int);
    }

    pub fn rcos(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        return ctx.value(c_int);
    }

    pub fn rsin(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        return ctx.value(c_int);
    }
};
