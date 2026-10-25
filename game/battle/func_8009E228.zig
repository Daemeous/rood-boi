//! Elevation angle from an actor's ground point to a target point (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_8009E228 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Elevation angle from the ground point under `actor` to `point`, or 0 when there is no point.
pub fn func_8009E228(actor: ?*v.D_800F4538_t, point: ?*v.SVECTOR) callconv(.c) c_int {
    var sp10: v.SVECTOR = undefined;
    const target = point orelse return 0;
    const self = actor.?;

    var var_s0: c_int = @as(c_int, target.vx) -% @as(c_int, self.unk0.position.vx);
    var_s0 = var_s0 *% var_s0;
    var var_v1: c_int = @as(c_int, target.vz) -% @as(c_int, self.unk0.position.vz);

    if (var_s0 < 0) {
        var_s0 = -%var_s0;
    }

    var_v1 = var_v1 *% var_v1;

    if (var_v1 < 0) {
        var_v1 = -%var_v1;
    }

    var_s0 = c.vs_gte_rsqrt(var_s0 +% var_v1);
    c.func_800A1AF8(@as(c_int, self.unk0.unkF), 0, psx.cptr(c.SVECTOR, &sp10), 0);
    return c.ratan2(@as(c_int, target.vy) -% @as(c_int, sp10.vy), var_s0);
}
