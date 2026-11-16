//! Checks the battle's party and enemy actors for the wind-up and pays out the charge
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_800790BC (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Scans the action queue's targets and, when none is still busy, sets the result passed to vs_battle_beginPostActionRecovery.
pub fn func_800790BC() callconv(.c) void {
    const base = v.g.vs_battle_actionState().*.?;
    var s4: c_int = 1;
    var i: c_int = 0;
    // uninitialised in the original when base.unk4 is 0; the port reads slot 0 there.
    var s3: *v.D_800F19CC_t2 = &base.unk854[0];

    while (psx.conv(c_uint, i) < base.unk4) : (i +%= 1) {
        s3 = &base.unk854[@intCast(@rem(i, 4))];
        var j: c_int = 0;
        while (j < @as(c_int, s3.unk4A)) : (j +%= 1) {
            const e = psx.at(&s3.unk4C, j);
            if (e.unk40 == 0 and (c.vs_actors_isActorActionAnimationBusy(@as(c_int, e.unk0.targetActor)) +% 1) > 1) {
                s4 = 0;
            }
        }
    }

    if (s4 != 0) {
        s4 = 1;
        i = 0;
        while (psx.conv(c_uint, i) < base.unk4) : (i +%= 1) {
            s3 = &base.unk854[@intCast(@rem(i, 4))];
            if (s3.unk4.unk40 == 0) {
                _ = c.vs_battle_beginActorDefeatAnimation(@as(c_int, s3.unk4.unk0.targetActor));
            }
            var j: c_int = 0;
            while (j < @as(c_int, s3.unk4A)) : (j +%= 1) {
                const e = psx.at(&s3.unk4C, j);
                if (e.unk40 == 0 and c.vs_battle_beginActorDefeatAnimation(@as(c_int, e.unk0.targetActor)) != 0) {
                    s4 = 2;
                }
            }
        }

        if (s3.unk4.unk0.targetActor != 0) {
            s4 = 0;
        }

        _ = c.vs_battle_beginPostActionRecovery(s4);
    }
}
