//! Refreshes the battle actor in slot `arg0` and posts its state message (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: vs_battle_playMeleeAttackAnimation (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c)
/// Refreshes the actor in slot `arg0` and posts the state message chosen from its distance (`arg1`, `arg2`).
pub fn vs_battle_playMeleeAttackAnimation(arg0: c_int, arg1: ?*v.SVECTOR, arg2: c_int) callconv(.c) void {
    const temp_s0: ?*v.D_800F4538_t = psx.at(v.g.vs_actors_actorRecords(), arg0).*;
    if (temp_s0) |s0| {
        s0.unk0.facingDelta = psx.conv(i16, c.vs_actors_actorFacingDeltaToPoint(@ptrCast(s0), @ptrCast(arg1)));
        s0.unk0.unk18 = 0xA;
        if (s0.unk63C >= 0x81) {
            s0.unk0.unk18 = 0x14;
        }

        const temp_v0: c_int = c.vs_actors_pitchToPoint(@ptrCast(s0), @ptrCast(arg1));

        var var_a1: c_int = 0x48;
        if (temp_v0 >= -0xE4) {
            var_a1 = 0x49;
            if (temp_v0 >= 0xAC) {
                var_a1 = 0x4A;
                if (temp_v0 >= 0x301 and arg2 == 0 and @as(c_int, s0.unk6E6) == 0x28) {
                    var_a1 = 0x4E;
                }
            }
        }

        _ = c.vs_actors_playActorAnimation(arg0, var_a1, 0, 6);
        c.vs_actors_headTrackingSpeed(@ptrCast(s0));
    }
}
