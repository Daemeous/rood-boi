//! Equips a shield on the character in `vs_battle_characterState` (src/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_equipShield (src/BATTLE/BATTLE.PRG/146C.c)
/// Applies `shield` to the character's stats block, then runs the two follow-up calls.
pub fn equipShield(shield: ?*v.vs_main_inventoryShield) callconv(.c) void {
    const actor = v.g.vs_battle_characterState().*.?;
    const stats = psx.get(actor.stats);
    // &stats->shield is computed without a dereference: a null stats block gives the field's offset, as in C.
    const shieldSlot: *align(1) v.vs_battle_uiShield = if (stats) |s|
        &s.shield
    else
        @ptrFromInt(@offsetOf(v.vs_battle_actor2, "shield"));

    c.vs_battle_applyShield(@ptrCast(shieldSlot), @ptrCast(shield));
    c.vs_battle_nop0(@ptrCast(stats));
    c.func_8006B214();
    c.func_8006B2D4();
}
