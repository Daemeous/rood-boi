//! Action availability flags for an actor: unaffordable, locked, status and weapon checks (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_getActionFlags (src/BATTLE/BATTLE.PRG/146C.c)
/// Flags (2: cost check fails, 4: action unusable) for action `actionId` of actor `actorId`.
pub fn getActionFlags(actorId: c_int, actionId: c_int) callconv(.c) c_int {
    const action = psx.at(v.g.actions(), actionId);
    const actorSlot = psx.at(v.g.vs_battle_actors(), actorId).*.?;
    const actor = psx.get(actorSlot.stats).?;
    var ret: c_int = @intFromBool(actor.unk954 != 0);

    const cost = c._getActionCost(psx.conv(c_uint, actionId), @ptrCast(actor), 0);
    if ((@as(c_uint, @bitCast(cost)) & 0xFF00_0000) == 0) {
        ret |= 2;
    }

    // unlocked << 15 is an int: zero exactly when the bit is clear.
    if (actorId == 0 and (@as(c_int, psx.getBits(action, "unlocked")) << 15) == 0) {
        ret |= 4;
    }

    if (actionId < 141) {
        if ((actor.statuses & 0x1000) != 0) {
            ret |= 4;
        }
    } else if (actionId < 224) {
        if (psx.at(v.g.vs_battle_actors(), actorId).*.?.weaponDrawn == 0) {
            ret |= 4;
        }
        if ((actor.statuses & 0x2000) != 0) {
            ret |= 4;
        }
    }
    return ret;
}
