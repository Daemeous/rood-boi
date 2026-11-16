//! Clears the two bytes at unk18D0 and the armature byte unkC of every bone
//! (pc/src/game/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");

/// orig: vs_actors_clearActorBonePoseLocks (pc/src/game/BATTLE/BATTLE.PRG/40564.c)
/// Clears the two bytes at unk18D0 and the armature unkC of each of the unit's bones.
pub fn vs_actors_clearActorBonePoseLocks(unit: ?*v.D_800F4538_t) callconv(.c) void {
    const s = unit.?;
    s.unk18D0[0] = 0;
    s.unk18D0[1] = 0;

    var i: c_int = 0;
    while (i < @as(c_int, s.unk0.nBones)) : (i +%= 1) {
        // The C re-reads unk68 on every iteration; the armature array is zero-length, so no bounds check.
        const armatures = &psx.get(s.unk0.model).?.armatures;
        psx.at(armatures, i).unkC = 0;
    }
}
