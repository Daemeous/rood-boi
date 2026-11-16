//! Battle block 146C: sets entity 29's armor vector from the lookup at vs_actors_actorPlacement (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_aimActionBehindSource (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Looks up the vector for `state.unk4` and stores it in entity 29's armor vector, its x and z rotated by the vector's angle.
pub fn vs_battle_aimActionBehindSource(state: ?*v.D_800F19CC_t) callconv(.c) void {
    const s = state.?;
    var sp10: v.func_8006EBF8_t = undefined;
    _ = c.vs_actors_actorPlacement(psx.conv(c_int, psx.conv(u8, s.unk4)), &sp10);

    const armor = @as(*align(1) v.SVECTOR, @ptrCast(&s.unk8.targets[29].armorDp));
    const angle: c_int = @as(c_int, sp10.location.position.pad) +% 0x800;
    armor.vx = psx.conv(i16, @as(c_int, sp10.location.position.vx) +% c.rsin(angle));
    armor.vz = psx.conv(i16, @as(c_int, sp10.location.position.vz) +% c.rcos(angle));
    armor.vy = sp10.location.position.vy;
}
