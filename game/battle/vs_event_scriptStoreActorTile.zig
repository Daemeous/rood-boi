//! Resolves an actor reference into a three-byte position written to state flags (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const c = @import("c_4A0A8");

/// orig: vs_event_scriptStoreActorTile (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Stores the position of the actor named in the script operand at `flag` and the two bytes after it, or 0xFF there when the actor is absent.
pub fn vs_event_scriptStoreActorTile(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    var position: [12]u8 = undefined;
    var actor: u16 = psx.conv(u16, c.vs_battle_getShort(arg0 + 1));
    const flag: u16 = psx.conv(u16, c.vs_battle_getShort(arg0 + 3));
    const flag0: c_short = psx.conv(c_short, @as(c_int, flag));
    const flag1: c_short = psx.conv(c_short, @as(c_int, flag) +% 1);
    const flag2: c_short = psx.conv(c_short, @as(c_int, flag) +% 2);

    if ((actor & 0x1000) != 0) {
        actor &= 0x1F;
        if (c.vs_actors_isBattleActorSlot(@as(c_int, actor)) == null) {
            c.vs_battle_setStateFlag(flag0, 0xFF);
            return 0;
        }
    } else {
        actor = psx.conv(u16, c.vs_actors_findActorByModelId(@as(c_int, actor)));
        if (actor == 1) {
            c.vs_battle_setStateFlag(flag0, 0xFF);
            return 0;
        }
    }
    _ = c.vs_actors_actorPlacement(@as(c_int, actor), &position);
    c.vs_battle_setStateFlag(flag0, position[0]);
    c.vs_battle_setStateFlag(flag1, position[1]);
    c.vs_battle_setStateFlag(flag2, position[2]);
    return 0;
}
