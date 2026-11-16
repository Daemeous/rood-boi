//! Drops or animates a battle actor from its slot, or resets its object slot when the actor is gone (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: vs_actors_beginActorDeath (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c)
/// Stops an actor's motion and starts its fall, then plays its animation (or holds it), and does the same for the actor linked to it; an empty slot resets its object instead.
pub fn vs_actors_beginActorDeath(actorId: c_int, context: ?*anyopaque, animate: c_int) callconv(.c) void {
    if (psx.at(v.g.vs_actors_actorRecords(), actorId).*) |actor| {
        const flags0: c_int = psx.fieldAt(c_int, &actor.unk0._bits_skip, 0).*;
        if ((flags0 & 0x270000) != 0 or (flags0 & 0x180000) == 0x100000) {
            const low = psx.getBits(&actor.unk0, "unkC_0");
            if (low != 0) {
                c.vs_field_setMovingPlatformActor(@as(c_int, low) -% 2, -1);
                psx.setBits(&actor.unk0, "unkC_0", 0);
            }
            actor.unk0.unk1A = 0;
            actor.unk0.unk34.vx = 0;
            actor.unk0.unk34.vz = 0;
            // The vertical speed is kept only when falling-free: unkF == 0, mode 3, vy > 0.
            const keepVy = actor.unk0.slot == 0 and
                (psx.fieldAt(c_int, &actor.unk0._bits_skip, 0).* & 0x70000) == 0x30000 and
                actor.unk0.unk34.vy > 0;
            if (!keepVy) actor.unk0.unk34.vy = -6;
            actor.unk181A = 0;
            psx.fieldAt(c_int, &actor.unk0._bits_skip, 0).* = (psx.fieldAt(c_int, &actor.unk0._bits_skip, 0).* & ~@as(c_int, 0x70000)) | 0x30000;
        }
        if (animate != 0) {
            var animationId: c_int = 0x46;
            if (psx.getBits(actor, "unk5AC_3") != 0) animationId = 0x3A;
            // Uninitialised in the original; the callee's result decides it, so it starts null here.
            var animation: [*c]c.u_short = null;
            _ = c.vs_actors_findActorAnimationHeader(@ptrCast(actor), 0xAB, &animation);
            if (animation != null) {
                if ((psx.fieldAt(c_int, &actor.unk0._bits_skip, 0).* & 0x70000) == 0) animationId = 0x47;
            }
            _ = c.vs_actors_playActorAnimation(actorId, animationId, 0, 6);
            c.vs_actors_headTrackingSpeed(@ptrCast(actor));
            psx.setBits(&actor.unk0, "unk9_0", 1);
        } else {
            psx.setBits(&actor.unk0, "unk9_0", 2);
        }
        if (actor.unk17FD >= 2) {
            c.vs_actors_beginActorDeath(@as(c_int, actor.unk17FD), context, animate);
        }
    } else {
        if (psx.at(v.g.vs_field_gimmickInstances(), actorId).*) |object| {
            object.unk1A = 0xF7;
            psx.fieldAt(u8, &object.unk6C, 260).* = 0;
            _ = c.vs_audio_playGimmickSoundPair(@ptrCast(object), 0x32, 0);
        }
    }
}
