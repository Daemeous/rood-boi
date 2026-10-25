//! Arms the timer-driven state of an actor in D_800F4538 (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_800A0618 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Sets the actor's unk5B2 countdown from arg3 less half the tick speed, runs its follow-up when positive, then leaves it at 1.
pub fn func_800A0618(arg0: c_int, arg1: c_int, arg2: c_int, arg3: c_int) callconv(.c) c_int {
    const v0 = c.func_800A0204(arg0, arg1, 0, arg2);

    if (v0 < 0) {
        return v0;
    }

    if (arg3 == 0) {
        return 0;
    }

    const remaining: c_int = arg3 -% @divTrunc(v.g.vs_gametime_tickspeed().*, 2);
    const temp_s0: *v.D_800F4538_t = psx.at(v.g.D_800F4538(), arg0).*.?;

    if (remaining > 0) {
        temp_s0.unk5B2 = psx.conv(u8, remaining);
        c.func_800AFA28(@ptrCast(temp_s0), @ptrCast(&temp_s0.unkC54), 1);
    }

    temp_s0.unk5B2 = 1;

    return 0;
}
