//! Distance from one actor's point to a target point, using the GTE reciprocal square root (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_actionTargetDistance (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns the distance between the current action's source point and its target point.
pub fn vs_battle_actionTargetDistance() callconv(.c) c_int {
    const d = v.g.vs_battle_actionState().*.?;
    const temp_s0 = psx.at(&d.queue, d.unk0);
    var from: v.SVECTOR = undefined;
    var to: v.SVECTOR = undefined;

    _ = c.func_800A1AF8(@as(c_int, temp_s0.primaryTarget.target.targetActor), 0, psx.cptr(c.SVECTOR, &from), 0);
    if (temp_s0.targetCount != 0) {
        if (temp_s0.targets[0].unk40 == 0) {
            _ = c.vs_battle_actionTargetLimbPosition(
                @as(c_int, temp_s0.targets[0].target.targetActor),
                @as(c_int, temp_s0.targets[0].target.targetLimb),
                psx.cptr(c.SVECTOR, &to),
            );
        } else {
            to = @as(*align(1) v.SVECTOR, @ptrCast(&temp_s0.targets[0].target)).*;
        }
    } else {
        _ = c.func_800A1AF8(@as(c_int, temp_s0.primaryTarget.target.targetActor), 0, psx.cptr(c.SVECTOR, &to), 0);
    }

    // Differences of two i16 values fit in an int: no overflow to wrap.
    const dx: c_int = @as(c_int, to.vx) - @as(c_int, from.vx);
    const dz: c_int = @as(c_int, to.vz) - @as(c_int, from.vz);
    const len_xz: c_int = c.vs_gte_rsqrt((dx *% dx) +% (dz *% dz));
    const dy: c_int = @as(c_int, to.vy) - @as(c_int, from.vy);

    return c.vs_gte_rsqrt((dy *% dy) +% (len_xz *% len_xz));
}
