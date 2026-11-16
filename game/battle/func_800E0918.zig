//! Movement check: runs a state's callback when it has reached its target or is clear of the floor (pc/src/game/BATTLE/BATTLE.PRG/771A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_771A8");
const c = @import("c_771A8");

const Callback = *const fn (?*v.vs_battle_aiState, c_int) callconv(.c) void;

/// orig: func_800E0918 (pc/src/game/BATTLE/BATTLE.PRG/771A8.c)
/// Runs the mode's callback on a movement state that has reached its target or cleared the floor; returns 1 when it did.
pub fn func_800E0918(state: ?*v.vs_battle_aiState, action: c_int, mode: c_int) callconv(.c) c_int {
    var callback: Callback = undefined;
    if (mode != 0) {
        callback = @ptrCast(&c.vs_battle_aiMoveActorTowardDestinationTile);
    } else {
        callback = @ptrCast(&c.func_800E4B70);
    }
    const s = state.?;

    if ((@as(c_int, s.flags) & 6) != 0) {
        const distance: c_uint = c.vs_core_mathSquaredTileDistance(psx.conv(c_uint, s.destination168.destination), psx.conv(c_uint, s.tile.sraw));
        if (mode != 0 and distance < 16) {
            if ((psx.get(s.object.movementCheckObject).?.flags & 0x200000) == 0) {
                callback(state, action);
                return 1;
            }
        } else if (v.g.vs_battle_aiRoomAiState().*.?.unk14 != 0) {
            if ((c.vs_field_groundHeightAtPackedTile(psx.conv(c_uint, s.tile.sraw)) +% @as(c_int, s.heightBE)) < v.g.vs_battle_aiRoomAiState().*.?.unk14) {
                if ((psx.get(s.object.movementCheckObject).?.flags & 0x200000) != 0) {
                    if (c.vs_battle_aiDestinationOccupancy(@ptrCast(s), psx.conv(c_uint, s.tile.sraw)) > 0) {
                        c.vs_battle_aiReleaseActorAiReservations(@ptrCast(s));
                        c.func_800E4B10(s);
                        return 1;
                    }
                } else {
                    callback(state, action);
                    return 1;
                }
            }
        }
    }
    return 0;
}
