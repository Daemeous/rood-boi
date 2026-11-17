//! Refreshes the per-zone record table (D_80061078) when the zone changes and ages the other records (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_restoreRoomEnemyCache (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Finds or replaces this zone's record, copies enemy data into it, and ages the other records.
pub fn vs_field_restoreRoomEnemyCache() callconv(.c) void {
    const zone = v.g._zoneContext();
    const records = v.g.D_80061078();
    var found: c_int = 0;
    var i: c_int = 0;
    var j: c_int = 0;

    while (j < 4) : (j +%= 1) {
        const rec = &records[@intCast(j)];
        if (rec.recency != 0 and @as(c_int, rec.zndId) == zone.zndId and @as(c_int, rec.mapId) == zone.mapId) {
            found = 1;
            v.g.vs_state_currentRoomStateSlot().* = j;
            while (psx.conv(c_uint, i) < zone.mpdEnemyCount) : (i +%= 1) {
                const temp: ?[*]u8 = @ptrCast(c.vs_actors_findRememberedEnemy(i));
                if (temp) |t| {
                    // The list is only dereferenced here, as in the C (its pointer may be null without a match).
                    psx.at(psx.get(zone.mpdEnemies).?, i).unk2 = t[1];
                }
            }
            break;
        }
    }

    if (found == 0) {
        i = 0;
        j = 0;
        while (j < 4) : (j +%= 1) {
            if (records[@intCast(j)].recency < records[@intCast(i)].recency) {
                i = j;
            }
        }
        v.g.vs_state_currentRoomStateSlot().* = i;
        c.vs_main_bzero(&records[@intCast(i)], @as(c_int, @sizeOf(v.vs_state_RoomEnemyStates)));
    }

    i = 0;
    while (i < 4) : (i +%= 1) {
        const cur: usize = @intCast(v.g.vs_state_currentRoomStateSlot().*);
        if (records[cur].recency < records[@intCast(i)].recency) {
            records[@intCast(i)].recency -%= 1;
        }
    }

    const slot: usize = @intCast(v.g.vs_state_currentRoomStateSlot().*);
    records[slot].recency = 4;
    records[slot].zndId = psx.conv(u8, zone.zndId);
    records[slot].mapId = psx.conv(u16, zone.mapId);
}
