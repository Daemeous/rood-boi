//! An event script opcode that turns an actor to a heading (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_scriptTurnActorToFacing (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Turns the actor of getShort(script + 1) to heading script[3] << 4 (script[5] the third
/// argument, 0xFF as -1): with script[4] 0 at once (vs_actors_turnActorToAtRate), else by the turn from its
/// vs_actors_actorPlacement heading, wrapped to a half circle and, for script[4] -1 or 1, forced
/// clockwise or counterclockwise (vs_actors_turnActorByAtRate); then its vs_event_scriptControlledActors mark.
pub fn vs_event_scriptTurnActorToFacing(script: [*c]u8, _: c_short) callconv(.c) c_int {
    var state: v.func_8006EBF8_t = undefined;
    const actor: u16 = psx.conv(u16, c.vs_event_resolveScriptActorReference(psx.conv(u16, c.vs_battle_getShort(script + 1))));
    const third: i16 = if (script[5] != 0xFF) script[5] else -1;
    if (script[4] != 0) {
        _ = c.vs_actors_actorPlacement(actor, @ptrCast(&state));
        var turn: c_int = (@as(c_int, script[3]) << 4) -% state.location.position.pad;
        if (turn < -0x1000 / 2) turn +%= 0x1000;
        if (turn > 0x1000 / 2) turn -%= 0x1000;
        switch (@as(i8, @bitCast(script[4]))) {
            -1 => {
                if (turn > 0) turn -%= 0x1000;
            },
            1 => {
                if (turn < 0) turn +%= 0x1000;
            },
            else => {},
        }
        c.vs_actors_turnActorByAtRate(actor, psx.conv(c_short, turn), third);
    } else {
        c.vs_actors_turnActorToAtRate(actor, psx.conv(c_short, @as(c_int, script[3]) << 4), third);
    }
    v.g.vs_event_scriptControlledActors()[actor] = 1;
    return 0;
}
