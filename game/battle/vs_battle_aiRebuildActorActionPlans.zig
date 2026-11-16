//! Clears each battle actor's evaluation state, runs the choice passes and acts on the results (pc/src/game/BATTLE/BATTLE.PRG/74088.c).
const psx = @import("common").psx;
const v = @import("views_74088");
const c = @import("c_74088");

const VoidPtrFn = *const fn (?*anyopaque) callconv(.c) void;
/// (A function, not a module-level pointer constant: that would be a relocated data slot in the engine library.)
inline fn planStateFn() VoidPtrFn {
    return @extern(VoidPtrFn, .{ .name = "vs_battle_aiBuildActorActionPlan" });
}
/// (A function, not a module-level pointer constant: that would be a relocated data slot in the engine library.)
inline fn actionEvaluatedFn() VoidPtrFn {
    return @extern(VoidPtrFn, .{ .name = "vs_battle_aiResetActorAwarenessIndicator" });
}
/// (A function, not a module-level pointer constant: that would be a relocated data slot in the engine library.)
inline fn choiceStateFn() VoidPtrFn {
    return @extern(VoidPtrFn, .{ .name = "vs_battle_aiSelectFallbackAction" });
}

/// orig: vs_battle_aiRebuildActorActionPlans (pc/src/game/BATTLE/BATTLE.PRG/74088.c)
/// Runs one action evaluation pass over the battle actors, with the context's actor as the chooser.
pub fn vs_battle_aiRebuildActorActionPlans(arg0: ?*anyopaque) callconv(.c) c_int {
    const context: ?*align(1) v.actorEvaluationContext = @ptrCast(arg0);

    v.g.vs_battle_aiRoomAiState().*.?.unk18 = 0;
    const heap: ?*anyopaque = c.vs_main_allocHeapR(0x1ECC);
    v.g.vs_battle_aiPlanningWorkspace().* = @ptrCast(@alignCast(heap));
    v.g.D_800F58EC().* = @ptrFromInt(@intFromPtr(heap) +% 0x9CC);
    v.g.D_800F5904().* = @ptrFromInt(@intFromPtr(heap) +% 0xD8C);
    v.g.D_800F58F8().* = @ptrFromInt(@intFromPtr(heap) +% 0x1D8C);

    if (v.g.vs_battle_aiPlanningWorkspace().* != null) {
        c.vs_battle_aiReconcileCompletedActionPlan(@ptrCast(@alignCast(arg0)));

        var actor: ?*v.vs_battle_actor = @ptrCast(v.g.vs_battle_actors()[0]);
        while (actor) |a| : (actor = psx.get(a.next)) {
            const state: *v.vs_battle_aiState = @ptrCast(psx.at(v.g.vs_battle_aiActorAiStates(), a.id).*.?);
            state.bE8 = 0;
            state.bE7 = 0;
            state.waitTimer = 0;
            const stats = psx.get(a.stats).?;
            c.vs_battle_aiSetActorVerticalMovementSteps(@as(c_int, state.id), @as(c_int, stats.unk31), @as(c_int, stats.unk33));
            choiceStateFn()(@ptrCast(state));
            if (context) |ctx| {
                if (ctx.f44 == 0 and ctx.id == state.id) state.timer = 0;
            }
        }

        c.vs_battle_aiApplyActionRelationshipChanges(@ptrCast(@alignCast(arg0)));

        actor = @ptrCast(v.g.vs_battle_actors()[0]);
        while (actor) |a| : (actor = psx.get(a.next)) {
            const state: *v.vs_battle_aiState = @ptrCast(psx.at(v.g.vs_battle_aiActorAiStates(), a.id).*.?);
            state.dead = @as(u8, @intFromBool(psx.get(a.stats).?.currentHP == 0));
        }

        actor = @ptrCast(v.g.vs_battle_actors()[0]);
        while (actor) |a| : (actor = psx.get(a.next)) {
            const state: *v.vs_battle_aiState = @ptrCast(psx.at(v.g.vs_battle_aiActorAiStates(), a.id).*.?);
            planStateFn()(@ptrCast(state));
            if (state.flag13 != 0) {
                if ((a.unk8 & 32) != 0) {
                    actionEvaluatedFn()(@ptrCast(state));
                } else {
                    c.vs_battle_aiSetActorAwarenessIndicator(@as(c_int, state.id), 128);
                }
            }
        }

        v.g.vs_battle_aiRoomAiState().*.?.unk18 = 0;
        if (v.g.vs_battle_aiPlanningWorkspace().*) |block| c.vs_main_freeHeapR(block);
    }
    return 0;
}
