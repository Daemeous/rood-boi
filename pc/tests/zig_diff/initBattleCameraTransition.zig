//! zig_diff spec: _initBattleCameraTransition -> init_battle_camera_transition.initBattleCameraTransition (146C.c).
//! Inputs: no transition yet (the heap stub hands out an arena block) or an
//! arena one; D_800F196C 7 or not; actor 0's position and facing
//! (func_800A1108's stub writes them) at the 16-bit edges; the camera
//! globals random. The camera stub records the target it is given (a
//! stack local the trace only tags).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").initBattleCameraTransition));

pub const classes = [_][:0]const u8{ "new-from-camera", "kept-from-camera", "running" };

var point: [4]i16 = .{ 0, 0, 0, 0 };

pub const stubs = struct {
    pub fn vs_main_allocHeapR(ctx: *zd.Ctx, size: anytype) ?*anyopaque {
        return @ptrCast(ctx.allocBytes(size, 16));
    }
    pub fn func_800A1108(ctx: *zd.Ctx, id: anytype, out: anytype) c_int {
        _ = id;
        const actor: *v.func_8006EBF8_t = @ptrCast(@alignCast(out.?));
        actor.unk0.unk4.vx = point[0];
        actor.unk0.unk4.vy = point[1];
        actor.unk0.unk4.vz = point[2];
        actor.unk0.unk4.pad = point[3];
        return ctx.value(c_int);
    }
    pub fn _computeCameraTransition(ctx: *zd.Ctx, target: anytype) void {
        const seen = ctx.alloc(v.VECTOR);
        seen.vx = target[0].vx;
        seen.vy = target[0].vy;
        seen.vz = target[0].vz;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const transition = v.g.vs_battle_cameraTransition();
    transition.* = if (ctx.chance(2)) null else ctx.alloc(v.vs_battle_cameraTransition_t);
    v.g.D_800F196C().* = ctx.edge(u32, &.{ 7, 0, 6, 8 });
    for (&point) |*p| p.* = ctx.pick(i16, &.{ 0, 0xB4, -1, 0x7FFF, -0x8000 });
    return .{};
}

pub fn classify(_: Args) usize {
    if (v.g.D_800F196C().* == 7) return 2;
    return if (v.g.vs_battle_cameraTransition().* == null) 0 else 1;
}
