//! Midpoint between the current action's position and its target point, written to an SVECTOR (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_actionCameraMidpoint (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Writes the midpoint between the current action's source and target points to `arg0`.
pub fn vs_battle_actionCameraMidpoint(arg0: ?*v.SVECTOR) callconv(.c) void {
    const d = v.g.vs_battle_actionState().*.?;
    // unk0 indexes unk854 without a check in the original.
    const temp_s0 = psx.at(&d.queue, d.unk0);
    var sp10: v.SVECTOR = undefined;
    var sp18: v.SVECTOR = undefined;

    c.func_800A1AF8(@as(c_int, temp_s0.primaryTarget.target.targetActor), 0, psx.cptr(c.SVECTOR, &sp10), 0);
    if (temp_s0.targetCount != 0) {
        if (temp_s0.targets[0].unk40 == 0) {
            c.vs_battle_actionTargetLimbPosition(
                @as(c_int, temp_s0.targets[0].target.targetActor),
                @as(c_int, temp_s0.targets[0].target.targetLimb),
                psx.cptr(c.SVECTOR, &sp18),
            );
        } else {
            sp18 = @as(*align(1) v.SVECTOR, @ptrCast(&temp_s0.targets[0].target)).*;
        }
    } else {
        sp18 = temp_s0.unk844;
    }

    arg0.?.vx = psx.conv(i16, @as(c_int, sp10.vx) +% @divTrunc(@as(c_int, sp18.vx) -% @as(c_int, sp10.vx), 2));
    arg0.?.vz = psx.conv(i16, @as(c_int, sp10.vz) +% @divTrunc(@as(c_int, sp18.vz) -% @as(c_int, sp10.vz), 2));
    arg0.?.vy = psx.conv(i16, @as(c_int, sp10.vy) +% @divTrunc(@as(c_int, sp18.vy) -% @as(c_int, sp10.vy), 2));
}
