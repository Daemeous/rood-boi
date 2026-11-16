//! Status flags of the controlled character for the battle menu (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_menu_frameworkActionRestrictionFlags (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Returns the character's state bits for the menu (0x2..0x100, plus 0x200 when arg0 is 2).
pub fn vs_menu_frameworkActionRestrictionFlags(arg0: c_int) callconv(.c) c_int {
    // The C reads the stats pointer before the call; it is dereferenced only after it.
    const actor = psx.get(v.g.vs_battle_characterState().*.?.stats);
    const temp_v0: c_int = psx.conv(c_int, c.vs_actors_actorStateFlags(0));
    var var_a0: c_int = @as(c_int, @intFromBool((temp_v0 & 0x80000) != 0)) * 2;

    if ((temp_v0 & 0x40100000) == 0x100000) {
        var_a0 |= 4;
    }

    // Re-read the global after the call, as the C does.
    const weapon_drawn = psx.getBits(psx.get(v.g.vs_battle_characterState().*.?.unk44).?, "weaponDrawn");
    if (weapon_drawn == 0) {
        var_a0 |= 8;
    }

    var ret: c_int = var_a0;

    if (actor.?.currentMP == 0) {
        ret |= 0x20;
    }

    if (actor.?.currentHP == 0) {
        ret |= 0x40;
    }

    if ((actor.?.statuses & (c.vsBattleStatusRegen | c.vsBattleStatusStrDown)) != 0) {
        ret |= 0x80;
    }

    if ((actor.?.statuses & c.vsBattleStatusWard) != 0) {
        ret |= 0x100;
    }

    if (arg0 == 2) {
        ret |= 0x200;
    }

    return ret;
}
