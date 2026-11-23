//! Resets a battle actor slot's placement and re-links it to its parent slot (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");

/// orig: vs_actors_attachActorToParentBone (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c)
/// Re-links actor slot `arg0` under parent slot `arg1` (kind `arg2`) and zeroes its position, pitch, roll and flag bit.
pub fn vs_actors_attachActorToParentBone(arg0: c_int, arg1: c_int, arg2: c_int, _: c_int) callconv(.c) void {
    const temp_a0: *v.D_800F4538_t = psx.at(v.g.vs_actors_actorRecords(), arg0).*.?;

    if (arg1 != -1) {
        if (temp_a0.unk0.attachParent != 0xFF) {
            if (psx.at(v.g.vs_actors_actorRecords(), @as(c_int, temp_a0.unk0.attachParent)).*) |other| {
                psx.setBits(other, "unk5AC_4", psx.conv(u2, @as(c_int, psx.getBits(other, "unk5AC_4")) -% 1));
            }
        }
        temp_a0.unk0.attachParent = psx.conv(u8, arg1);
        temp_a0.unk0.unk13 = psx.conv(u8, arg2);
    }

    // The C stores 4-byte zeros over the SVECTOR halves (vx/vy, then vz/pad).
    psx.fieldAt(c_int, &temp_a0.unk0.position.vx, 0).* = 0;
    psx.fieldAt(c_int, &temp_a0.unk0.position.vz, 0).* = 0;
    temp_a0.unk0.translationFrames = 0;
    psx.fieldAt(c_int, &temp_a0.unk0.pitch, 0).* = 0;
    psx.fieldAt(c_int, &temp_a0.unk0.roll, 0).* = 0;
    temp_a0.unk0.turnFrames = 0;
    psx.setBits(&temp_a0.unk0, "unk9_5", 1);
}
