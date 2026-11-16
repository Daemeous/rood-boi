//! Spell cost of the chosen action, scaled by the hit parameter's stat factor
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: _factorSpellCost (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// The spell cost of the current action times the stat factor of hit parameter `arg3`, over 10; 0 for non-spells.
pub fn factorSpellCost(action: ?*v.vs_action_t, _: ?*v._hitEntity_t, _: ?*v._hitEntity_t, arg3: c_int, _: c_int) callconv(.c) c_short {
    // The action index is a u16 and the table has 256 entries: the C does not check it.
    const index: u16 = v.g.vs_battle_actionState().*.?.unk8.actionIndex;
    const spell = psx.at(v.g.actions(), index);

    var result: c_short = 0;
    if (psx.getBits(spell, "type") == 1) {
        // cost is u8 and statFactor a 5-bit field: both promote to int, so the product cannot overflow.
        const hitParam = psx.at(&action.?.hitParams, arg3);
        const product: c_int = @as(c_int, spell.cost) *% @as(c_int, psx.getBits(hitParam, "statFactor"));
        result = psx.conv(c_short, @divTrunc(product, 10));
    }

    return result;
}
