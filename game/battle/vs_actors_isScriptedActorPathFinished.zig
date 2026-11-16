//! Battle actor check: runs the actor's assembly callee and clears a flag bit on a zero result (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");

/// orig: vs_actors_isScriptedActorPathFinished (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Runs the actor's check; on 0 clears bit 2 of the actor's flags and returns 1, else returns whether the check gave 2.
pub fn vs_actors_isScriptedActorPathFinished(arg0: c_int) callconv(.c) c_int {
    // ROOD_PC: the assembly callee reads the actor id from $a0, so it is passed as arg0.
    const Check = *const fn (c_int) callconv(.c) c_int;
    const check: Check = @ptrCast(&c.vs_battle_aiActorNavigationOverrideStatus);
    const temp_v0 = check(arg0);

    if (temp_v0 == 0) {
        const getFlags = @extern(*const fn (c_int) callconv(.c) c_int, .{ .name = "vs_field_actorCollisionBypass" });
        const flags = getFlags(arg0);
        c.vs_field_setActorCollisionBypass(arg0, flags & ~@as(c_int, 2));
        return 1;
    }
    return @as(c_int, @intFromBool(temp_v0 == 2));
}
