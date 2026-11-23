//! Arms the timer-driven state of an actor in vs_actors_actorRecords (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: vs_actors_playActorAnimationAtFrame (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c)
/// Sets the actor's unk5B2 countdown from arg3 less half the tick speed, runs its follow-up when positive, then leaves it at 1.
pub fn vs_actors_playActorAnimationAtFrame(arg0: c_int, arg1: c_int, arg2: c_int, arg3: c_int) callconv(.c) c_int {
    const v0 = c.vs_actors_playActorAnimation(arg0, arg1, 0, arg2);

    if (v0 < 0) {
        return v0;
    }

    if (arg3 == 0) {
        return 0;
    }

    const remaining: c_int = arg3 -% @divTrunc(v.g.vs_gametime_tickspeed().*, 2);
    const temp_s0: *v.D_800F4538_t = psx.at(v.g.vs_actors_actorRecords(), arg0).*.?;

    if (remaining > 0) {
        temp_s0.framesPerStep = psx.conv(u8, remaining);
        c.func_800AFA28(@ptrCast(temp_s0), @ptrCast(&temp_s0.pose), 1);
    }

    temp_s0.framesPerStep = 1;

    return 0;
}
