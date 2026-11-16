//! Frees the battle zone's enemy and file buffers and its MPD LBA table (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_releaseZoneData (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Tears down the zone's loaded data: enemy and file buffers when present, then the MPD LBA table.
pub fn vs_field_releaseZoneData() callconv(.c) void {
    c.func_800BEBEC();
    c._vsBattleNop2(0);
    const zndEnemies = psx.get(v.g._zoneContext().zndEnemies);
    if (zndEnemies != null) c.vs_main_freeHeap(@ptrCast(zndEnemies.?));
    const zudFiles = psx.get(v.g._zoneContext().zudFiles);
    if (zudFiles != null) c.vs_main_freeHeap(@ptrCast(zudFiles.?));
    c.vs_main_freeHeap(@ptrCast(psx.get(v.g._zoneContext().mpdLbas)));
}
