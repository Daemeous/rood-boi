//! Unit: vs_battle_aiNotifyActionRelationshipChanges from pc/src/game/BATTLE/BATTLE.PRG/74088.c.
const common = @import("common");
const psx = common.psx;
const v = @import("views_74088");
const c = @import("c_74088");

/// orig: vs_battle_aiNotifyActionRelationshipChanges (pc/src/game/BATTLE/BATTLE.PRG/74088.c)
/// Clears the entry flag at 0x9A of the entry this actor links to (when it has no link value), then calls vs_battle_aiApplyActionRelationshipChanges on the actor.
pub fn vs_battle_aiNotifyActionRelationshipChanges(arg0: ?*v.func_800DEEA4_t) callconv(.c) void {
    if (arg0) |actor| {
        if (actor.unk44 == 0) {
            // Unchecked as in the C (unk4 past the 18 entries reads on).
            const entry = psx.at(v.g.vs_battle_aiActorAiStates(), actor.unk4).*;
            if (entry) |e| {
                e.timer = 0;
            }
        }

        c.vs_battle_aiApplyActionRelationshipChanges(psx.cptr(c.func_800DEEA4_t, actor));
    }
}
