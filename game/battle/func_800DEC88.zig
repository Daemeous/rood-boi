//! Clears each battle actor's evaluation state, runs the choice passes and acts on the results (src/BATTLE/BATTLE.PRG/74088.c).
const psx = @import("common").psx;
const v = @import("views_74088");
const c = @import("c_74088");

const VoidPtrFn = *const fn (?*anyopaque) callconv(.c) void;
/// (A function, not a module-level pointer constant: that would be a relocated data slot in the engine library.)
inline fn planStateFn() VoidPtrFn {
    return @extern(VoidPtrFn, .{ .name = "func_800DE3E4" });
}
/// (A function, not a module-level pointer constant: that would be a relocated data slot in the engine library.)
inline fn actionEvaluatedFn() VoidPtrFn {
    return @extern(VoidPtrFn, .{ .name = "func_800D821C" });
}
/// (A function, not a module-level pointer constant: that would be a relocated data slot in the engine library.)
inline fn choiceStateFn() VoidPtrFn {
    return @extern(VoidPtrFn, .{ .name = "func_800DD604" });
}

/// orig: func_800DEC88 (src/BATTLE/BATTLE.PRG/74088.c)
/// Runs one action evaluation pass over the battle actors, with the context's actor as the chooser.
pub fn func_800DEC88(arg0: ?*anyopaque) callconv(.c) c_int {
    const context: ?*align(1) v.actorEvaluationContext = @ptrCast(arg0);

    v.g.D_800F58BC().*.?.unk18 = 0;
    const heap: ?*anyopaque = c.vs_main_allocHeapR(0x1ECC);
    v.g.D_800F5900().* = @ptrCast(@alignCast(heap));
    v.g.D_800F58EC().* = @ptrFromInt(@intFromPtr(heap) +% 0x9CC);
    v.g.D_800F5904().* = @ptrFromInt(@intFromPtr(heap) +% 0xD8C);
    v.g.D_800F58F8().* = @ptrFromInt(@intFromPtr(heap) +% 0x1D8C);

    if (v.g.D_800F5900().* != null) {
        c.func_800DEB10(@ptrCast(@alignCast(arg0)));

        var actor: ?*v.actorEvaluationActor = @ptrCast(v.g.vs_battle_actors()[0]);
        while (actor) |a| : (actor = psx.get(a.next)) {
            const state: *v.actorEvaluationState = @ptrCast(psx.at(v.g.D_800F5878(), a.id).*.?);
            state.fE8 = 0;
            state.fE7 = 0;
            state.f19A = 0;
            const stats = psx.get(a.stats).?;
            c.func_800E685C(@as(c_int, state.id), @as(c_int, stats.f31), @as(c_int, stats.f33));
            choiceStateFn()(@ptrCast(state));
            if (context) |ctx| {
                if (ctx.f44 == 0 and ctx.id == state.id) state.timer = 0;
            }
        }

        c.func_800DC810(@ptrCast(@alignCast(arg0)));

        actor = @ptrCast(v.g.vs_battle_actors()[0]);
        while (actor) |a| : (actor = psx.get(a.next)) {
            const state: *v.actorEvaluationState = @ptrCast(psx.at(v.g.D_800F5878(), a.id).*.?);
            state.dead = @as(u8, @intFromBool(psx.get(a.stats).?.hp == 0));
        }

        actor = @ptrCast(v.g.vs_battle_actors()[0]);
        while (actor) |a| : (actor = psx.get(a.next)) {
            const state: *v.actorEvaluationState = @ptrCast(psx.at(v.g.D_800F5878(), a.id).*.?);
            planStateFn()(@ptrCast(state));
            if (state.f13 != 0) {
                if ((a.flags & 32) != 0) {
                    actionEvaluatedFn()(@ptrCast(state));
                } else {
                    c.func_800D820C(@as(c_int, state.id), 128);
                }
            }
        }

        v.g.D_800F58BC().*.?.unk18 = 0;
        if (v.g.D_800F5900().*) |block| c.vs_main_freeHeapR(block);
    }
    return 0;
}
