//! The PC's analog movement hooks (pc/src/analog_move.c) the game's code asks.
const psx = @import("psx.zig");
const object = @import("object.zig");

const CardinalFn = *const fn (c_int) callconv(.c) c_int;
const StepsFn = *const fn (c_int, c_int, c_int) callconv(.c) c_int;

/// The nearest of the four tile directions of Ashley's facing (0 -z, 1 -x, 2 +z, 3 +x) from `(facing + 0x200) & 0xFFF`:
/// that over 1024, what the original gives, unless analog movement is on and the stick was the last input: then the
/// direction chosen before holds until the facing has moved analog_hysteresis degrees past the boundary.
pub fn cardinal(biased: c_int) c_int {
    if (psx.weak(CardinalFn, "pc_analog_cardinal")) |f| return f(biased);
    return @divTrunc(biased, 1024);
}

/// Whether actor `actor` (slot `slot`) has a pose step due this frame: Ashley's stride at the pace of her speed under a
/// continuous analog pace (the frames due, whole, with the fraction carried; then her step count is that), every
/// actor otherwise as in the original (one step, two when hasted).
pub fn stepDue(actor: anytype, slot: c_int) bool {
    const steps = object.animationSteps(actor);
    if (slot != 0) return true;
    const f = psx.weak(StepsFn, "pc_analog_anim_steps") orelse return true;
    const n = f(slot, steps.*, actor.animationId);
    steps.* = @intCast(n);
    return n > 0;
}

const PoseBeginFn = *const fn () callconv(.c) void;
const PoseActorFn = *const fn (*anyopaque, *anyopaque) callconv(.c) void;
const PoseWeaponFn = *const fn (*anyopaque, *anyopaque) callconv(.c) void;

/// The start of a frame's actor pass (pc/src/pose_blend.c): no actor's pose is blended for the draw until asked.
pub fn poseBegin() void {
    if (psx.weak(PoseBeginFn, "pc_pose_blend_begin")) |f| f();
}

/// Ashley's slowed stride, drawn between its ticks (setting anim_blend): after her pose step, before her bone chain is
/// built and drawn. Only the draw's matrices change; nothing the game keeps does.
pub fn poseActor(actor: *anyopaque, camera: *anyopaque) void {
    if (psx.weak(PoseActorFn, "pc_pose_blend_actor")) |f| f(actor, camera);
}

/// One of her weapons before its chain is built on her bone (the same blend for what she holds).
pub fn poseWeapon(weapon: *anyopaque, actor: *anyopaque) void {
    if (psx.weak(PoseWeaponFn, "pc_pose_blend_weapon")) |f| f(weapon, actor);
}
