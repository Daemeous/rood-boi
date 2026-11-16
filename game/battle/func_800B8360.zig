//! An event script opcode that hands an actor a 2D vector with mode -4 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B8360 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls vs_actors_startHeadTracking for the actor of getShort(script + 1) with -4, the vector
/// (getShort(+3), getShort(+5), 0) and script[7] (0 as 0x1000).
pub fn func_800B8360(script: [*c]u8, _: c_short) callconv(.c) c_int {
    var vec: v.SVECTOR = undefined;
    vec.vx = c.vs_battle_getShort(script + 3);
    vec.vy = c.vs_battle_getShort(script + 5);
    vec.vz = 0;
    // pad is uninitialised in the original and vs_actors_startHeadTracking copies the whole vector into the
    // actor (unk17F4.pad): 0 here (PORTING.md §11.4).
    vec.pad = 0;
    const actor: c_int = c.vs_event_resolveScriptActorReference(psx.conv(u16, c.vs_battle_getShort(script + 1)));
    var speed: c_int = 0x1000;
    if (script[7] != 0) speed = script[7];
    _ = c.vs_actors_startHeadTracking(actor, -4, psx.cptr(c.SVECTOR, &vec), speed);
    return 0;
}
