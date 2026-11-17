//! Battle ability input: applies the ability's effect to the target of the current
//! ability slot (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_applyActionSourceEffects (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Applies the current battle ability's effect to actor `arg0` when it is that ability's target.
pub fn vs_battle_applyActionSourceEffects(arg0: c_int, _: ?*v.func_8006CE70_t) callconv(.c) void {
    if (arg0 >= 16) {
        return;
    }

    const base = v.g.vs_battle_actionState().*.?;
    const t: *v.vs_battle_ActionRequest = &base.queue[@as(usize, base.unk0 & 3)];

    if (@as(c_int, t.primaryTarget.target.targetActor) == arg0 and t.primaryTarget.unk40 == 0) {
        switch (@as(c_int, t.primaryTarget.target.unk3)) {
            2, 3, 4 => {},
            0, 1 => c.func_8009EA14(arg0, psx.cptr(c.SVECTOR, &t.unk844)),
            5 => {
                if (arg0 == 0) {
                    c.func_8009E070(0, null, 6);
                }
            },
            else => {},
        }

        c._applyBattleAbilityEffect(@ptrCast(&t.primaryTarget));
    }
}
