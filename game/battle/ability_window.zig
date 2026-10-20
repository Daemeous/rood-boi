//! The chain and defense ability timing window (src/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_80078828 (src/BATTLE/BATTLE.PRG/146C.c)
/// 1 when the current action allows a battle ability: with `button` 0 whether any is mapped, else that button's (1-3) ability and its cost.
pub fn canUseBattleAbility(button: c_int) callconv(.c) c_int {
    const block = v.g.D_800F19CC().*.?;
    const settings = v.g.vs_main_settings();
    // unk0 & 3: below the 4 queued actions.
    const current = &block.unk854[block.unk0 & 3];
    var ret: c_int = 1;

    if (current.unk4.unk40 == 0 and current.unk4A != 0 and current.unk4C[0].unk40 == 0) {
        if (current.unk4.unk0.targetActor == 0) {
            var hasMappedAbility: c_int = 0;
            if (button == 0) {
                var i: c_int = 0;
                while (i < 3) : (i += 1) {
                    ret = 1;
                    if (settings.mappedChainAbilities[@intCast(i)] != 0) {
                        hasMappedAbility = 1;
                    }
                }
                if (hasMappedAbility == 0) {
                    ret = 0;
                }
            }
        } else {
            var hasMappedAbility: c_int = 0;
            if (button == 0) {
                var i: c_int = 0;
                while (i < 3) : (i += 1) {
                    if (settings.mappedDefenseAbilities[@intCast(i)] != 0) {
                        hasMappedAbility = 1;
                    }
                }
                if (hasMappedAbility == 0) {
                    ret = 0;
                }
            }
        }

        if (current.unk4C[0].unk0.targetActor == 0) {
            _ = c.func_800A0BE0(0);
        } else if (psx.at(v.g.vs_battle_actors(), current.unk4C[0].unk0.targetActor).*.?.unk27 == 0x80) {
            ret = 0;
        }

        if (current.unk4.unk0.targetActor != 0) {
            if (current.unk4C[0].unk0.targetActor != 0) {
                ret = 0;
            }
        } else if (current.actionIndex >= 40) {
            ret = 0;
        }

        // actionIndex - 40u: an unsigned comparison (40..53).
        if (psx.conv(c_uint, @as(c_int, current.actionIndex)) -% 40 < 14) {
            ret = 0;
        }

        if (current.unk4.unk0.targetActor == current.unk4C[0].unk0.targetActor) {
            ret = 0;
        }

        if (button != 0) {
            const stats = psx.get(v.g.vs_battle_characterState().*.?.stats);
            if (current.unk4.unk0.targetActor == 0) {
                // mappedChainAbilities[button - 1]: the C does not check the button.
                if (psx.conv(c_uint, c._getActionCost(psx.at(&settings.mappedChainAbilities, button -% 1).*, @ptrCast(stats), 0)) & 0xFF000000 == 0) {
                    ret = 0;
                }
            } else {
                if (psx.conv(c_uint, c._getActionCost(psx.at(&settings.mappedDefenseAbilities, button -% 1).*, @ptrCast(stats), 0)) & 0xFF000000 == 0) {
                    ret = 0;
                }
            }

            if (@as(c_int, block.previousChainInput) == button) {
                ret = 0;
            }
        }

        if (block.battleAbilityInputAttempted != 0) {
            ret = 0;
        }
    } else {
        ret = 0;
    }

    return ret;
}
