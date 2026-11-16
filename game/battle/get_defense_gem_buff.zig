//! Gem defense buff for one hit of an action (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");

/// True when one of the actor's three shield gems has `effect` as its gemEffects.
fn hasGemEffect(actor: *v.vs_battle_actor2, effect: u8) bool {
    var found = false;
    for (0..3) |i| {
        if (actor.shield.gems[i].gemEffects == effect) found = true;
    }
    return found;
}

/// orig: _getDefenseGemBuff (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns 20 for each gem bonus that applies to the action's hit on the actor, else 0.
pub fn getDefenseGemBuff(action: ?*v.vs_action_t, actor: ?*v.vs_battle_actor2, hitNumber: c_int) callconv(.c) c_short {
    var gemBuff: c_int = 0;
    const a = actor.?;
    if ((v.g.vs_battle_actors()[a.actorId].?.weaponDrawn & 1) != 0) {
        const act = action.?;
        if (psx.getBits(act, "type") == 1) {
            if (hasGemEffect(a, 6)) gemBuff +%= 20;
        } else {
            const id: c_uint = act.id;
            if (id < 40 or ((id -% 54) & 0xFF) < 170) {
                if (hasGemEffect(a, 5)) gemBuff +%= 20;
            }
        }

        const hit = psx.at(&act.hitParams, hitNumber);
        const effect: c_int = psx.getBits(hit, "effect");
        const wanted: ?u8 = switch (effect) {
            1 => 11,
            2 => 12,
            3 => 13,
            5 => 14,
            11 => 16,
            15 => 18,
            16 => 19,
            17 => 21,
            else => null,
        };
        if (wanted) |gem| {
            if (hasGemEffect(a, gem)) gemBuff +%= 20;
        }
    }
    return psx.conv(c_short, gemBuff);
}
