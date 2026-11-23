//! Per-frame update of the battle unit's bone pose and its copy of the
//! previous pose (pc/src/game/BATTLE/BATTLE.PRG/44F14.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_44F14");
const c = @import("c_44F14");

/// orig: vs_actors_updateActorAnimationBlend (pc/src/game/BATTLE/BATTLE.PRG/44F14.c)
/// Advances the unit's pose animation one step, or copies the current pose into the working copy when no step is due.
pub fn vs_actors_updateActorAnimationBlend(unit_opt: ?*v.D_800F4538_t) callconv(.c) void {
    const unit = unit_opt.?;
    c.func_800AFA28(@ptrCast(unit), @ptrCast(&unit.pose), 0);

    tail: {
        if (unit.unk5CC != 0) {
            if (unit.unk5CC == 1) {
                c.vs_actors_alignPoseRotationBranches(@ptrCast(&unit.blendPose.unk0), @ptrCast(&unit.pose.unk0), unit.unk0.nBones);
            }

            const v5b2: c_int = unit.framesPerStep;

            if (v5b2 != 1) {
                const next: c_int = @as(c_int, unit.unk5CC) + 0xFF;
                unit.unk5CC = psx.conv(u8, v5b2 +% next);

                if (unit.unk5CC >= unit.unk5CD) {
                    unit.unk5CC = 0;
                    unit.unk5CD = 0;
                    break :tail;
                }
            }

            c.vs_actors_blendActorPose(
                @ptrCast(&unit.blendPose),
                @ptrCast(&unit.pose),
                unit.unk0.nBones,
                unit.unk5CC,
                unit.unk5CD,
            );

            psx.setBits(&unit.blendPose, "holdingLastFrame", psx.getBits(&unit.pose, "holdingLastFrame"));
            psx.setBits(&unit.blendPose, "reachedEnd", psx.getBits(&unit.pose, "reachedEnd"));

            c.vs_actors_updateMotionAndLookAt(@ptrCast(unit), 0);

            unit.unk5CC +%= 1;

            if (unit.unk5CC >= unit.unk5CD) {
                unit.unk5CC = 0;
                unit.unk5CD = 0;
            }
            return;
        }
    }

    c.vs_main_memcpy(@ptrCast(&unit.blendPose), @ptrCast(&unit.pose), @as(c_uint, @sizeOf(v.D_800F4538_unkC54)));
    c.vs_actors_updateMotionAndLookAt(@ptrCast(unit), 0);
}
