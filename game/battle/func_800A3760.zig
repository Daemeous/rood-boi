//! Nearest-actor search over the battle actor table (src/BATTLE/BATTLE.PRG/3A1A0.c).
const psx = @import("common").psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// orig: func_800A3760 (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Index of the nearest actor to `selfIndex` within the given horizontal range, vertical band and 90 degrees of facing, or -1.
pub fn func_800A3760(selfIndex: c_int, maxRange: c_int, maxDy: c_int) callconv(.c) c_int {
    const table = v.g.D_800F4538();
    var best: c_int = -1;
    // The C does not bounds-check the index.
    const selfActor: *v.D_800F4538_t = psx.at(table, selfIndex).* orelse return -1;
    const maxDist: c_int = maxRange *% maxRange;
    var bestDist: c_int = undefined;

    var i: c_int = 0;
    while (i < 17) : (i +%= 1) {
        if (i == selfIndex) continue;
        const actor = table[@intCast(i)] orelse continue;
        if (psx.getBits(&actor.unk0, "skip") != 0) continue;

        const dy: c_int = @as(c_int, actor.unk0.position.vy) -% @as(c_int, selfActor.unk0.position.vy);
        if (dy < -%maxDy or dy > maxDy) continue;

        const dx: c_int = @as(c_int, actor.unk0.position.vx) -% @as(c_int, selfActor.unk0.position.vx);
        var dz: c_int = @as(c_int, actor.unk0.position.vz) -% @as(c_int, selfActor.unk0.position.vz);

        var dist: c_int = dx *% dx;
        if (dist < 0) dist = -%dist;
        dz = dz *% dz;
        if (dz < 0) dz = -%dz;
        dist +%= dz;
        if (dist > maxDist) continue;

        const angle = c.func_8009E180(@ptrCast(selfActor), psx.cptr(c.SVECTOR, &actor.unk0.position));
        // 4096 / 4: a quarter turn either side of facing.
        if (angle < -1024 or angle > 1024) continue;

        if (best == -1 or dist < bestDist) {
            best = i;
            bestDist = dist;
        }
    }
    return best;
}
