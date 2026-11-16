//! Weapon affinity update after an action's class and armor changes (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_items_growAttackerWeaponRatings (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Applies an action's class and armor affinity changes to the actor's blade, then recalculates its weapon class affinity.
pub fn vs_items_growAttackerWeaponRatings(action: ?*v.vs_action_t, arg1: ?*v.vs_battle_actor2, arg2: c_int, arg3: c_int) callconv(.c) void {
    const act = action.?;
    if (@as(c_int, psx.getBits(act, "type")) != c.actionTypeSpell) {
        const actor = arg1.?;
        const temp_s1: *v.vs_battle_uiEquipment = &actor.weapon.blade;
        if (actor.weapon.blade.id != 0) {
            if (arg2 != 0) {
                c._triggerClassChange(@ptrCast(act), @ptrCast(temp_s1), arg2 -% 1, 0xF0, @as(c_int, actor.unk957));
            }
            if (arg3 != 0) {
                c._triggerArmorAffinityChange(@ptrCast(act), @ptrCast(temp_s1), arg3 -% 1, 0xF0, @as(c_int, actor.unk957));
            }
        }
        c._calculateWeaponClassAffinity(@ptrCast(actor));
    }
}
