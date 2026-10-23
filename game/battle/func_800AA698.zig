//! Moves battle actor `slot` by a step vector, or sets its move counter (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: func_800AA698 (src/BATTLE/BATTLE.PRG/40564.c)
/// Adds `step` to actor `slot`'s position (mode 0), or sets its move count (mode -1 derives it from the velocity's length).
pub fn func_800AA698(slot: c_int, step: ?*v.SVECTOR, mode: c_int) callconv(.c) void {
    const actor: *v.D_800F4538_t = psx.at(v.g.D_800F4538(), slot).* orelse return;
    const s = step.?;
    actor.unk0.unk34.vx = s.vx;
    actor.unk0.unk34.vy = s.vy;
    actor.unk0.unk34.vz = s.vz;

    if (mode == 0) {
        actor.unk0.unk1A = 0;
        applyMove(actor, s);
        return;
    }

    if (mode == -1) {
        var temp: c_int = @as(c_int, actor.unk0.unk34.vx);
        temp = temp *% temp;
        if (temp < 0) temp = -%temp;
        var dist: c_int = temp;

        temp = @as(c_int, actor.unk0.unk34.vz);
        temp = temp *% temp;
        if (temp < 0) temp = -%temp;
        dist = dist +% temp;

        temp = @as(c_int, actor.unk0.unk34.vy);
        temp = temp *% temp;
        if (temp < 0) temp = -%temp;

        dist = psx.conv(c_int, c.vs_gte_rsqrt(dist +% temp)) *% @as(c_int, actor.unk5C2);
        actor.unk0.unk1A = psx.conv(u16, @divTrunc(dist, 128));
        if ((dist & 0x7F) != 0) {
            actor.unk0.unk1A = psx.conv(u16, @divTrunc(dist, 128) +% 1);
        }
        if (actor.unk0.unk1A == 0) {
            applyMove(actor, s);
        }
        return;
    }

    actor.unk0.unk1A = psx.conv(u16, mode);
}

/// The move tail of func_800AA698 (its `move:` label): adds the step to the position and updates the tile.
fn applyMove(actor: *v.D_800F4538_t, s: *const v.SVECTOR) void {
    const pos = &actor.unk0.position;
    pos.vx = psx.conv(i16, @as(c_int, pos.vx) +% @as(c_int, s.vx));
    pos.vy = psx.conv(i16, @as(c_int, pos.vy) +% @as(c_int, s.vy));
    pos.vz = psx.conv(i16, @as(c_int, pos.vz) +% @as(c_int, s.vz));
    actor.unk0.currentTileX = psx.conv(u8, @divTrunc(@as(c_int, pos.vx), 128));
    actor.unk0.currentTileZ = psx.conv(u8, @divTrunc(@as(c_int, pos.vz), 128));
}
