//! Hit factor of the previous hit: the player's HP+MP times the action's stat factor, plus the unit's counter
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: _previousHitFactor0 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns the previous hit's factor for action hit parameter `index`, from the first actor's HP and MP.
pub fn previousHitFactor0(action: ?*v.vs_action_t, _: ?*v._hitEntity_t, _: ?*v._hitEntity_t, index: c_int, _: c_int) callconv(.c) c_short {
    const game = v.g.vs_battle_actionState().*.?;
    const entity = &game.unk8.targets[0].target;
    const hp: c_int = entity.hp;
    const mp: c_int = entity.mp;
    // hitParams[index]: the C does not check the index against the array's two entries.
    const params = psx.at(&action.?.hitParams, index);
    const statFactor: c_int = psx.getBits(params, "statFactor");
    const quotient = @divTrunc((hp +% mp) *% statFactor, 10);
    // unk0 is u32, so the sum is unsigned in C; the low 16 bits are what the short keeps.
    const sum: c_uint = psx.conv(c_uint, quotient) +% (game.unk0 -% 1);
    return psx.conv(c_short, sum);
}
