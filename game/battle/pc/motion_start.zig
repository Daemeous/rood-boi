//! Actor move requests (D2: 4A0A8.c's inline assembly; oracle pc/src/battle_motion.c).
const c = @import("c_battle_motion");
const psx = @import("common").psx;
const v = @import("views_battle_motion");

/// orig: motion_reset (pc/src/battle_motion.c)
/// Clears the motion flags (byte 0xA bits 5-6), sets bit 6, then hands the
/// target to vs_actors_beginActorJumpToTile.
fn motionReset(model: *v.D_800F4538_t, target: ?[*]c_int, speed: c_int, facing: c_int) void {
    const moving = psx.fieldAt(u8, &model.unk0._bits_skip, 2);
    moving.* = (moving.* & 0x9f) | 0x40;
    c.vs_actors_beginActorJumpToTile(model, target, speed, facing);
}

/// orig: motion_animated (pc/src/battle_motion.c)
/// Sets motion bits 5-6, plays animation 0x1F (blend 8) and hands the target
/// to vs_actors_beginActorJumpToTile.
fn motionAnimated(id: c_int, model: *v.D_800F4538_t, target: ?[*]c_int, speed: c_int, facing: c_int) void {
    psx.fieldAt(u8, &model.unk0._bits_skip, 2).* |= 0x60;
    _ = c.vs_actors_playActorAnimation(id, 0x1f, 0, 8);
    c.vs_actors_beginActorJumpToTile(model, target, speed, facing);
}

/// orig: vs_actors_beginTileJump (pc/src/battle_motion.c)
/// Starts a move: an actor already moving (bit 6 of byte 0xA) restarts it, one
/// with bit 5 animates first; otherwise the low two bits of byte 0x5B0 become 1.
pub fn startMove(id: c_int, target: ?[*]c_int, speed: c_int, facing: c_int) callconv(.c) void {
    const model = v.g.vs_actors_actorRecords()[@intCast(id)].?;
    const moving = psx.fieldAt(u8, &model.unk0._bits_skip, 2);
    if (moving.* & 0x40 != 0) {
        motionReset(model, target, speed, facing);
        return;
    }
    if (moving.* & 0x20 != 0) {
        motionAnimated(id, model, target, speed, facing);
        return;
    }
    const mode = psx.fieldAt(u8, &model._bits_unk5AC_0, 4);
    mode.* = (mode.* & 0xfc) | 1;
    c.vs_actors_beginActorJumpToTile(model, target, speed, facing);
}

/// orig: vs_actors_beginAirborneTileJump (pc/src/battle_motion.c)
/// Restarts the actor's move at the target (motion_reset).
pub fn restartMove(id: c_int, target: ?[*]c_int, speed: c_int, facing: c_int) callconv(.c) void {
    motionReset(v.g.vs_actors_actorRecords()[@intCast(id)].?, target, speed, facing);
}
