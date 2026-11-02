//! Queues the room's MPD file for loading (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: _loadMpd (src/BATTLE/BATTLE.PRG/146C.c)
/// Queues MPD file `id` of the zone (its LBA and size from mpdLbas) into a fresh heap block, the slot and block kept
/// in _zoneContext (vs_main_nop9 notes a slot or block still held).
pub fn loadMpd(id: c_int) callconv(.c) void {
    const zone = v.g._zoneContext();
    var file: v.vs_main_CdFile = undefined;
    file.lba = psx.at(psx.get(zone.mpdLbas).?, id).lba;
    file.size = psx.at(psx.get(zone.mpdLbas).?, id).size;
    if (psx.get(zone.mpdCdFile) != null) {
        c.vs_main_nop9(0x89, 0);
    }
    psx.put(&zone.mpdCdFile, @as(?*v.vs_main_CdQueueSlot, @ptrCast(@alignCast(c.vs_main_allocateCdQueueSlot(@ptrCast(&file))))));
    if (psx.get(zone.mpdData) != null) {
        c.vs_main_nop9(0x8A, 0);
    }
    psx.put(&zone.mpdData, c.vs_main_allocHeapR(psx.conv(c_uint, file.size)));
    c.vs_main_cdEnqueuePriority(@ptrCast(psx.get(zone.mpdCdFile)), psx.get(zone.mpdData));
}
