//! Splits an actor's pending X, Z and Y offsets into whole units for an SVECTOR
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");

/// orig: vs_actors_consumeActorMovementOffset (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Moves whole units (4096 each) of the actor's pending X, Z and Y offsets into the vector, capping each step at 8 and carrying the rest.
pub fn vs_actors_consumeActorMovementOffset(arg0: ?*v.vs_battle_actor, arg1: ?*v.SVECTOR) callconv(.c) void {
    const actor = arg0.?;
    const vec = arg1.?;

    if (actor.unkC < 0) {
        if (actor.unkC >= -0x7FFF) {
            vec.vx = psx.conv(i16, @divTrunc(actor.unkC, 4096));
            actor.unkC = 0;
        } else {
            vec.vx = -8;
            actor.unkC +%= 0x8000;
        }
    } else if (actor.unkC <= 0x7FFF) {
        vec.vx = psx.conv(i16, @divTrunc(actor.unkC, 4096));
        actor.unkC = 0;
    } else {
        vec.vx = 8;
        actor.unkC -%= 0x8000;
    }

    if (actor.unk10.s32 < 0) {
        if (actor.unk10.s32 >= -0x7FFF) {
            vec.vz = psx.conv(i16, @divTrunc(actor.unk10.s32, 4096));
            actor.unk10.s32 = 0;
        } else {
            vec.vz = -8;
            actor.unk10.s32 +%= 0x8000;
        }
    } else if (actor.unk10.s32 <= 0x7FFF) {
        vec.vz = psx.conv(i16, @divTrunc(actor.unk10.s32, 4096));
        actor.unk10.s32 = 0;
    } else {
        vec.vz = 8;
        actor.unk10.s32 -%= 0x8000;
    }

    if (actor.unk14 < 0) {
        if (actor.unk14 >= -0x7FFF) {
            vec.vy = psx.conv(i16, @divTrunc(actor.unk14, 4096));
            actor.unk14 = 0;
        } else {
            vec.vy = -8;
            actor.unk14 +%= 0x8000;
        }
    } else if (actor.unk14 <= 0x7FFF) {
        vec.vy = psx.conv(i16, @divTrunc(actor.unk14, 4096));
        actor.unk14 = 0;
    } else {
        vec.vy = 8;
        actor.unk14 -%= 0x8000;
    }
}
