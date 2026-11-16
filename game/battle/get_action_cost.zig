//! Resource cost of a battle action and whether the actor can pay it (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: _getActionCost (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Packs requirements-met (bit 24), action type (bits 16..23) and cost; with updateStat set, deducts the cost from the actor's MP, risk, HP or weapon PP.
pub fn getActionCost(actionId: c_uint, actor: ?*v.vs_battle_actor2, updateStat: c_int) callconv(.c) c_int {
    var requirementsMet: c_int = undefined;
    var actionType: c_int = undefined;
    var cost: c_int = undefined;
    // The original's inner switch has no default: statValue is uninitialised there. Read as 0.
    var statValue: c_int = 0;

    const action = psx.at(v.g.actions(), actionId);
    cost = @as(c_int, action.cost);
    actionType = @as(c_int, psx.getBits(action, "type"));

    switch (actionType) {
        c.actionTypeSpell => {
            statValue = @as(c_int, actor.?.currentMP);
        },
        c.actionTypeAbility => {
            statValue = @as(c_int, 100) -% @as(c_int, actor.?.risk);
        },
        c.actionTypeBreakArt => {
            statValue = @as(c_int, actor.?.currentHP);
            cost +%= 1;
        },
        4 => {
            statValue = @as(c_int, actor.?.weapon.currentPp);
        },
        c.actionTypeBaseAttack => {
            const a = actor.?;
            cost = @as(c_int, a.weapon.risk);
            switch (@as(c_int, a.weapon.actionType)) {
                c.actionTypeSpell => {
                    statValue = @as(c_int, a.currentMP);
                },
                c.actionTypeAbility => {
                    statValue = @as(c_int, 100) -% @as(c_int, a.risk);
                },
                c.actionTypeBreakArt => {
                    statValue = @as(c_int, a.currentHP);
                    cost +%= 1;
                },
                4 => {
                    statValue = @as(c_int, a.weapon.currentPp);
                },
                else => {},
            }
            actionType = @as(c_int, a.weapon.actionType);
        },
        else => {
            statValue = 0;
        },
    }

    if (actionType == c.actionTypeAbility) {
        const d = v.g.vs_battle_actionState().*.?;
        if (d.unk4 >= 10) {
            if ((actionId -% 22) < 18) {
                cost = psx.conv(c_int, psx.conv(c_uint, cost) +% ((d.unk4 *% d.unk4) / 10));
            }
        }
    }

    if (statValue >= cost) {
        if (actionType == c.actionTypeBreakArt) {
            cost -%= 1;
        }
        statValue -%= cost;
        requirementsMet = 1;
    } else if (actionType == c.actionTypeAbility) {
        statValue = 0;
        requirementsMet = 1;
    } else {
        requirementsMet = 0;
    }

    if (updateStat != 0) {
        switch (actionType) {
            c.actionTypeSpell => {
                actor.?.currentMP = psx.conv(i16, statValue);
            },
            c.actionTypeAbility => {
                actor.?.risk = psx.conv(u16, @as(c_int, 100) -% statValue);
            },
            c.actionTypeBreakArt => {
                actor.?.currentHP = psx.conv(i16, statValue);
            },
            4 => {
                actor.?.weapon.currentPp = psx.conv(u16, statValue);
            },
            else => {},
        }
    }

    return (requirementsMet << 24) +% (actionType << 16) +% cost;
}
