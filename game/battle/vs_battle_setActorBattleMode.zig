//! Weapon-drawn state of battle actors, and the actor's stance flag (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_setActorBattleMode (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Toggles (arg1 == 2) or sets the weapon-drawn state of actor arg0; actor 0 also updates the character's HUD.
pub fn vs_battle_setActorBattleMode(arg0: c_int, arg1: c_int) callconv(.c) void {
    if (arg1 == 2) {
        const actor = psx.at(v.g.vs_battle_actors(), arg0).*.?;
        actor.weaponDrawn ^= 1;

        if ((actor.weaponDrawn & 1) != 0) {
            c.vs_actors_setActorWeaponDrawn(arg0, 1, 0);
        } else {
            c.vs_actors_setActorWeaponDrawn(arg0, 0, 0);
        }
    } else if (arg0 != 0) {
        const actor = psx.at(v.g.vs_battle_actors(), arg0).*.?;
        if (actor.allegiance == 4 or actor.allegiance == 8) {
            if (arg1 != 0) {
                actor.allegiance = 4;
            } else {
                actor.allegiance = 8;
            }
        }
    } else {
        const head = v.g.vs_battle_actors()[0].?;
        head.weaponDrawn &= ~@as(c_int, 1);
        head.weaponDrawn |= arg1;
        c.vs_actors_setActorWeaponDrawn(0, arg1, 1);
    }

    if (arg0 == 0) {
        const character = v.g.vs_battle_characterState().*.?;
        c.vs_battle_setHudBattleMode(character.weaponDrawn & 1);
    }
}
