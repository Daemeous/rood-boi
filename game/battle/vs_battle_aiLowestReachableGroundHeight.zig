//! Scans the directional cache around an actor for the lowest height among its
//! directions (pc/src/game/BATTLE/BATTLE.PRG/771A8.c).
const common = @import("common");
const pad = @import("common").pad;
const psx = common.psx;
const v = @import("views_771A8");
const c = @import("c_771A8");

/// orig: vs_battle_aiLowestReachableGroundHeight (pc/src/game/BATTLE/BATTLE.PRG/771A8.c)
/// Returns the lowest height sampled at the offsets of the directions selected by `arg1`, or 0.
pub fn vs_battle_aiLowestReachableGroundHeight(arg0: ?*v.vs_battle_aiState, arg1: c_uint) callconv(.c) c_int {
    var min: c_int = 0;
    var count: c_int = 1;

    if ((arg1 & 0x3FF) != 0) {
        count = 3;
    }
    var slot: c_uint = (arg1 >> 9) & 6;
    while (true) {
        count -%= 1;
        if (count == -1) break;
        if (c.vs_battle_aiCanFlyInDirection(@ptrCast(arg0), psx.conv(c_int, slot)) != 0) {
            // Scratchpad direction table: one byte per slot, dx and dz are 2-bit signed fields.
            const entry: *align(1) v.D_800F16EC_t = &pad.directions(v.D_800F16EC_t)[slot];
            const h = c.vs_field_groundHeightAtTile(
                @as(c_int, arg0.?.tile.b[0]) +% @as(c_int, psx.getBits(entry, "dx")),
                @as(c_int, arg0.?.tile.b[2]) +% @as(c_int, psx.getBits(entry, "dz")),
            );
            if (h < min) {
                min = h;
            }
        }
        slot = (slot +% 1) & 7;
    }
    return min;
}
