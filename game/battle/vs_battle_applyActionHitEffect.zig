//! Applies one action hit to its target: the effect mediator, the spell status
//! rules, and the weapon-arts kill count (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

const Mediator = *const fn (?*v.vs_action_t, ?*v._hitEntity_t, ?*v._hitEntity_t, c_int, c_int) callconv(.c) void;

/// The stats block of the battle actor `target` (vs_battle_actors[target]->unk3C).
fn targetStats(actors: [*]?*v.vs_battle_actor, target: u8) *v.vs_battle_actor2 {
    return psx.get(actors[target].?.stats).?;
}

/// The action's type bitfield as an int.
fn actionKind(action: ?*v.vs_action_t) c_int {
    return @as(c_int, psx.getBits(action.?, "type"));
}

/// _actionEffectMediators[action->hitParams[hit].effect](action, arg1, arg2, hit, arg3).
fn callMediator(action: ?*v.vs_action_t, arg1: ?*v._hitEntity_t, arg2: ?*v._hitEntity_t, hit: c_int, arg3: c_int) void {
    const effect = psx.getBits(psx.at(&action.?.hitParams, hit), "effect");
    const mediators = psx.GlobalMany(?Mediator, "_actionEffectMediators").ptr();
    mediators[effect].?(action, arg1, arg2, hit, arg3);
}

/// orig: vs_battle_applyActionHitEffect (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Applies hit `hit` of `action` from `arg1` to `arg2`, then counts a weapon-arts kill when that hit defeats the target.
pub fn vs_battle_applyActionHitEffect(action: ?*v.vs_action_t, arg1: ?*v._hitEntity_t, arg2: ?*v._hitEntity_t, arg3: c_int, hit: c_int) callconv(.c) void {
    const e2 = arg2.?;
    const actors = v.g.vs_battle_actors();

    if (e2.unk40 == 0 and targetStats(actors, e2.unk0.targetActor).unk957 == 0x80) {
        e2.unk0.hp = psx.conv(i16, @as(c_int, e2.unk0.hp) +% 999);
        psx.setBits(e2, "hpEffect", 1);
    } else if (actionKind(action) == c.actionTypeSpell) {
        if (e2.unk40 == 0 and (psx.conv(u32, targetStats(actors, e2.unk0.targetActor).statuses) & ((@as(u32, 1) << 31) | 0x40000)) != 0) {
            v.g._lastValue().* = 0;
        } else {
            callMediator(action, arg1, arg2, hit, arg3);
        }
        if (e2.unk40 == 0) {
            const st = targetStats(actors, e2.unk0.targetActor);
            if (st.unk957 != 0x80) {
                e2.inflictStatuses |= st.statuses & 0x61000;
            }
        }
    } else {
        callMediator(action, arg1, arg2, hit, arg3);
    }

    if (arg3 != 0 and arg1.?.unk40 == 0 and arg1.?.unk0.targetActor == 0 and
        e2.unk40 == 0 and
        targetStats(actors, e2.unk0.targetActor).unk957 != 0x80 and
        (actionKind(action) == c.actionTypeAbility or
            actionKind(action) == c.actionTypeBreakArt or
            actionKind(action) == c.actionTypeBaseAttack) and
        targetStats(actors, e2.unk0.targetActor).currentHP <= e2.unk0.hp and
        e2.unk0.targetActor != 0 and
        actors[e2.unk0.targetActor].?.unk29 == 0)
    {
        const target = actors[e2.unk0.targetActor].?;
        target.unk29 = 1;
        const cs = v.g.vs_battle_characterState().*.?;
        const temp_s0: u8 = cs.equippedWeaponCategory % 10;
        const arts = v.g.vs_main_artsStatus();
        if (c.vs_battle_getStateFlag(0x82) >= 0xA and
            arts.artsLearned[temp_s0] < 4 and
            arts.kills.weaponCategories[temp_s0] < v.g.vs_main_artsPointsRequirements()[temp_s0][arts.artsLearned[temp_s0]])
        {
            arts.kills.weaponCategories[temp_s0] +%= 1;
        }
    }
}
