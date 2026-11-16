//! Weapon PP of a hit's target actor, plus the battle ability counter less one
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: _getWeaponPp (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns the target actor's current weapon PP plus the battle ability counter minus one.
pub fn getWeaponPp(_: ?*v.vs_action_t, hit: ?*v._hitEntity_t, _: ?*v._hitEntity_t, _: c_int, _: c_int) callconv(.c) c_short {
    const actor = v.g.vs_battle_actors()[hit.?.target.targetActor].?;
    const stats = psx.get(actor.stats).?;
    // (u_short)vs_battle_actionState->unk0: truncated to 16 bits, then promoted to int.
    const counter: c_int = psx.conv(u16, v.g.vs_battle_actionState().*.?.unk0);
    return psx.conv(c_short, @as(c_int, stats.weapon.currentPp) +% (counter -% 1));
}
