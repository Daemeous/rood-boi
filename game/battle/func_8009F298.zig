//! Battle helper for the 30DB0 unit (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_8009F298 (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c)
/// Runs the unit's setup for `slot` and, when `flag` is 1, calls vs_actors_queueActorAnimation for slot 0xFE if its entry reports a result.
pub fn func_8009F298(slot: c_int, vec: ?*v.SVECTOR, flag: c_int) callconv(.c) void {
    const vec_c: [*c]c.SVECTOR = if (vec) |p| psx.cptr(c.SVECTOR, p) else null;
    c.func_8009EFEC(slot, vec_c, flag);
    if (flag == 1) {
        var sp10: [*c]c.u_short = null;
        _ = c.vs_actors_findActorAnimationHeader(@ptrCast(psx.at(v.g.vs_actors_actorRecords(), slot).*), 0xFE, &sp10);
        if (sp10 != null) {
            _ = c.vs_actors_queueActorAnimation(slot, 0xFE, 8, 0);
        }
    }
}
