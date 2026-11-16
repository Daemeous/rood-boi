//! Per-tick battle pass over the first actor table and the enemy table (pc/src/game/BATTLE/BATTLE.PRG/3A1A0.c).
const psx = @import("common").psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// orig: vs_actors_updateRoomActorMovement (pc/src/game/BATTLE/BATTLE.PRG/3A1A0.c)
/// Runs the per-tick update of each live entry of the first table, then of each unskipped enemy, and sets D_800F4B18.
pub fn vs_actors_updateRoomActorMovement() callconv(.c) void {
    var i: usize = 0;
    while (i < 16) : (i += 1) {
        if (v.g.vs_field_gimmickInstances()[i]) |p| {
            if (p.unk0 != 0) {
                c.vs_field_updateGimmickMotion(@ptrCast(p), @divTrunc(v.g.vs_gametime_tickspeed().*, 2));
            }
        }
    }

    i = 0;
    while (i < 17) : (i += 1) {
        if (v.g.vs_actors_actorRecords()[i]) |p| {
            if (psx.getBits(&p.unk0, "skip") == 0) {
                c.vs_actors_updateActorMovement(@intCast(i));
            }
        }
    }

    v.g.D_800F4B18().* = 1;
}
