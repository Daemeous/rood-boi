//! Starts an action: validates it, sets up the context and its cooldown (pc/src/game/BATTLE/BATTLE.PRG/71618.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_71618");
const c = @import("c_71618");

/// orig: vs_battle_aiTryExecuteActionCandidate (pc/src/game/BATTLE/BATTLE.PRG/71618.c)
/// Sets up `action` in `context` and starts its cooldown; returns 1 when the action was started.
pub fn vs_battle_aiTryExecuteActionCandidate(context: ?*v.vs_battle_aiState, action: ?*v.func_800DCAA0_t) callconv(.c) c_int {
    if (c.vs_battle_aiCanExecuteActionCandidate(@ptrCast(context), @ptrCast(action)) == 0) return 0;
    c.vs_battle_aiTryEngageActor(@ptrCast(context));
    if (c.vs_battle_aiIsEngagementReady(@ptrCast(context)) == 0) return 0;

    const ctx = context.?;
    const act = action.?;
    ctx.candidate = @ptrCast(act);
    v.g.D_800F58E0().* = 1;

    const actor = v.g.vs_actors_actorRecords()[psx.getBits(act, "unkC_0")];
    if (actor != null and (psx.fieldAt(c_uint, &actor.?.unk0._bits_skip, 0).* & 0x180000) != 0) {
        psx.setBits(act, "unkC_4", psx.conv(u3, c.vs_battle_aiNextActorRandomMasked(2)));
        psx.setBits(act, "unkC_7", 217);
    }

    c.vs_battle_aiIssueActorCommand(@ptrCast(ctx), 15);
    if (c.vs_battle_aiDoesActionRequireCooldown(@ptrCast(ctx), @ptrCast(act)) != 0) {
        if (ctx.flag13 == 0) {
            const bc = v.g.vs_battle_aiRoomAiState().*.?;
            ctx.minDelay = psx.byteAt(u8, bc, 8).*;
        } else {
            ctx.minDelay = 50;
        }
    }
    return 1;
}
