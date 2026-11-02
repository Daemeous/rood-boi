//! zig_diff spec: _handleCombatCameraZoom -> handle_combat_camera_zoom.handleCombatCameraZoom (146C.c).
//! Inputs: the camera's delta mode (1 or 3 aim, others not) and yaw (0 or
//! not), its distance and the projection at the edges of the 0x600/0x900 and
//! 0x300 clamps (one step of 0xC0 or 0x40 away, at, either side); the zoom
//! kind D_800F19CC->unk2C0E 0, 1, 2 or another. Stubs: func_800780A8 fills
//! the target vector; _computeStepAndDisplacement fills the step and a
//! displacement that is zero or not, and may change the delta's yaw and mode
//! (so the settle test's later conditions both hold and fail).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").handleCombatCameraZoom));

pub const classes = [_][:0]const u8{ "not-aiming", "step", "widen", "push", "other" };

pub const stubs = struct {
    pub fn func_800780A8(ctx: *zd.Ctx, target: anytype) void {
        const t: *v.SVECTOR = @ptrCast(@alignCast(target.?));
        t.vx = ctx.value(i16);
        t.vy = ctx.value(i16);
        t.vz = ctx.value(i16);
    }
    pub fn _computeStepAndDisplacement(ctx: *zd.Ctx, target: anytype, step: anytype, displacement: anytype, clamp: anytype, speed: anytype) c_int {
        _ = target;
        _ = clamp;
        _ = speed;
        const s: *v.VECTOR = @ptrCast(@alignCast(step.?));
        const d: *v.VECTOR = @ptrCast(@alignCast(displacement.?));
        s.vx = ctx.value(i32);
        s.vy = ctx.value(i32);
        s.vz = ctx.value(i32);
        d.vx = 0;
        d.vy = 0;
        d.vz = 0;
        switch (ctx.value(u8) % 8) {
            0 => d.vx = ctx.value(i32) | 1,
            1 => d.vy = ctx.value(i32) | 1,
            2 => d.vz = ctx.value(i32) | 1,
            else => {},
        }
        const camera = v.g.vs_battle_cameraCurrentSpherical();
        switch (ctx.value(u8) % 8) {
            0 => camera.delta.yaw = ctx.value(i32) | 1,
            1 => camera.delta.mode = 0,
            2 => camera.delta.mode = 3,
            3 => camera.delta.mode = 1,
            else => {},
        }
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.alloc(v.D_800F19CC_t);
    v.g.D_800F19CC().* = state;
    state.unk2C0E = ctx.edge(i16, &.{ 0, 0, 1, 1, 1, 2, 2, 2, 3, -1 });
    const camera = v.g.vs_battle_cameraCurrentSpherical();
    camera.delta.mode = ctx.edge(i32, &.{ 1, 1, 3, 3, 0, 2, -1, 4 });
    camera.delta.yaw = if (ctx.chance(3)) ctx.int(i32) else 0;
    camera.values.distance = ctx.pick(i32, &.{ 0x5FF, 0x600, 0x601, 0x6BF, 0x6C0, 0x6C1, 0x700, 0x83F, 0x840, 0x841, 0x8FF, 0x900, 0x901 });
    v.g.vs_main_projectionDistance().* = ctx.pick(i32, &.{ 0x200, 0x2BF, 0x2C0, 0x2C1, 0x2FF, 0x300, 0x301 });
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const mode = v.g.vs_battle_cameraCurrentSpherical().delta.mode;
    if (mode != 1 and mode != 3) return 0;
    return switch (v.g.D_800F19CC().*.?.unk2C0E) {
        0 => 1,
        1 => 2,
        2 => 3,
        else => 4,
    };
}
