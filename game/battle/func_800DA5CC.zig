//! Target lookup for the da4bc state: tries the entry's target, then the state's own target
//! (pc/src/game/BATTLE/BATTLE.PRG/71618.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_71618");
const c = @import("c_71618");

/// orig: func_800DA5CC (pc/src/game/BATTLE/BATTLE.PRG/71618.c)
/// Resolves the state's target, returning 1 when a target was handled and 0 otherwise.
pub fn func_800DA5CC(state: ?*v.vs_battle_aiState) callconv(.c) c_int {
    const s = state.?;
    const entry = psx.get(s.entries.da4bc[0]);

    if (s.status == 0 and c.vs_battle_aiTryApproachPlannedTarget(@ptrCast(s), @ptrCast(entry)) != 0) return 1;

    const target: c_int = @as(c_int, s.target16);
    if (target < 16) {
        if (c.vs_battle_aiApproachActor(@ptrCast(s), target, 288, 1) != 0) {
            if (s.rangeMode == 1 and s.mode12E != 7) {
                // The entry's flagsC target field; a null entry reads through the C's NULL pointer.
                const et: *v.da4bcEntryTarget = @ptrCast(&entry.?.flagsC);
                s.movementTargetId = psx.conv(u8, @as(c_int, psx.getBits(et, "target")));
                c.vs_battle_aiReleaseActorAiReservations(@ptrCast(s));
                c.vs_battle_aiRecoverMovement(@ptrCast(s));
            }
            return 1;
        }
        if (c.vs_battle_aiTrackActorPosition(@ptrCast(s), target) != 0) return 1;
    }
    return 0;
}
