//! Resets a battle actor's risk, HP and MP and refills its limb HP (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_actors_restoreActorVitals (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Restores actor slot `arg0`'s risk, HP and MP to their maxima and its limbs to full, then clears D_800F5160.
pub fn vs_actors_restoreActorVitals(arg0: c_int) callconv(.c) void {
    const slot = psx.at(v.g.vs_battle_actors(), arg0).*;
    const actor = slot orelse return;
    const temp_a1 = psx.get(actor.stats) orelse return;

    temp_a1.risk = 0;
    temp_a1.actionRecoveryTimer = 0;
    temp_a1.currentHP = temp_a1.maxHP;
    temp_a1.currentMP = temp_a1.maxMP;

    var i: c_int = 0;
    while (i < 6) : (i +%= 1) {
        const limb = &temp_a1.limbs[@intCast(i)];
        limb.hp = limb.maxHp;
    }

    c.vs_battle_removeActorStatuses(0x7FFFFFFF, @ptrCast(temp_a1));
    v.g.D_800F5160().* = 0;
}
