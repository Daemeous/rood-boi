//! An actor's turn toward a facing, with its turn animation (pc/src/game/BATTLE/BATTLE.PRG/38C1C.c).
const psx = @import("common").psx;
const v = @import("views_38C1C");
const c = @import("c_38C1C");

/// orig: vs_actors_playActorTurnAnimation (pc/src/game/BATTLE/BATTLE.PRG/38C1C.c)
/// Starts the turn of actor `actorId` toward `facing` (0x1000 a full turn) unless it is already there or busy.
pub fn turnToward(actorId: c_int, facing: c_int) callconv(.c) void {
    const actor: *v.D_800F4538_t = psx.at(psx.GlobalMany(?*v.D_800F4538_t, "vs_actors_actorRecords").ptr(), actorId).*.?;

    if (psx.getBits(&actor.unk0, "unkA_5") != 0) {
        return;
    }

    var turn: c_int = @as(c_int, facing) -% @as(c_int, actor.unk0.facing);

    if (turn > 0x800) {
        turn -%= 0x1000;
    }

    if (turn < -0x800) {
        turn +%= 0x1000;
    }

    if (turn == 0) {
        return;
    }

    actor.unk1848.unk0 = 6;

    const v1: u8 = actor.animationId;

    if (v1 == 0x76 or actor.animationId == 0x75 or psx.getBits(&actor.unk0, "unkC_0") != 0) {
        return;
    }

    var animation: c_int = 0x1F;

    if (psx.getBits(&actor.unk0, "unkA_6") == 0) {
        animation = 0x11;

        if (turn < 0) {
            animation = 0x12;
        }
    }

    c.vs_actors_playActorAnimation(actorId, animation, 0, 0xA);
}
