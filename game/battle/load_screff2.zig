//! Loads or releases the SCREFF2 overlay (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: _loadScreff2 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Nonzero: queues SCREFF2.PRG (lba 135000, 4096 bytes) into overlay slot 1 and marks it loading; zero: once the read is done (state 4), frees the slot, clears the mark and waits.
pub fn loadScreff2(load: c_int) callconv(.c) void {
    const p = v.g.D_800EB9B4().*.?;
    if (load != 0) {
        p.unk0 = 1;
        var file: v.vs_main_CdFile = undefined;
        file.lba = 135000;
        file.size = 4096;
        psx.put(&p.slot, @as(?*v.vs_main_CdQueueSlot, @ptrCast(c.vs_main_allocateCdQueueSlot(@ptrCast(&file)))));
        c.vs_main_cdEnqueue(@ptrCast(psx.get(p.slot)), v.g.vs_overlay_slots()[1]);
        return;
    }
    if (psx.get(p.slot).?.state == 4) {
        c.vs_main_freeCdQueueSlot(@ptrCast(psx.get(p.slot)));
        p.unk0 = 0;
        c.vs_main_wait();
    }
}
