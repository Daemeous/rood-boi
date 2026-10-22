//! The effect purge file's load: queues or completes the read of EFFPURGE.BIN
//! into overlay slot 3 (src/BATTLE/BATTLE.PRG/6F014.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_6F014");
const c = @import("c_6F014");

/// orig: _loadfile (src/BATTLE/BATTLE.PRG/6F014.c)
/// Queues a CD read of lba/size into buf, or once it has loaded frees its slot and reports done.
fn loadFile(lba: c_int, size: c_int, buf: ?*anyopaque) c_int {
    if (v.g.D_800F568C().* == null) {
        var cdFile: c.vs_main_CdFile = std.mem.zeroes(c.vs_main_CdFile);
        cdFile.lba = psx.conv(@TypeOf(cdFile.lba), lba);
        cdFile.size = psx.conv(@TypeOf(cdFile.size), size);
        const slot: ?*c.vs_main_CdQueueSlot = @ptrCast(c.vs_main_allocateCdQueueSlot(&cdFile));
        v.g.D_800F568C().* = @ptrCast(slot);
        c.vs_main_cdEnqueue(@ptrCast(slot), buf);
        return 1;
    }

    const slot = v.g.D_800F568C().*.?;
    if (@as(c_int, slot.state) == @as(c_int, c.vs_main_CdQueueStateLoaded)) {
        c.vs_main_freeCdQueueSlot(@ptrCast(slot));
        v.g.D_800F568C().* = null;
        return 0;
    }

    return 1;
}

/// orig: vs_battle_loadEffPurge (src/BATTLE/BATTLE.PRG/6F014.c)
/// Starts the effect purge on first call, then queues or completes EFFPURGE.BIN's load; returns 1 while it is pending.
pub fn loadEffPurge() callconv(.c) c_int {
    if (v.g.D_800F5684().* == 0) {
        _ = c.func_8007E180(2);
        v.g.D_800F5684().* = 1;
    }

    // VS_EFFPURGE_BIN_LBA, VS_EFFPURGE_BIN_SIZE.
    return loadFile(115000, 18432, v.g.vs_overlay_slots()[3]);
}
