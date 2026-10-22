//! The battle overlay's pending file load: queues or completes the read of
//! vs_battle_pFileLoadContext (src/BATTLE/BATTLE.PRG/6F014.c).
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

/// orig: func_800D7BF8 (src/BATTLE/BATTLE.PRG/6F014.c)
/// Allocates the file buffer on first use, then loads the battle file queued in vs_battle_pFileLoadContext.
pub fn func_800D7BF8() callconv(.c) c_int {
    const ctx = v.g.vs_battle_pFileLoadContext();
    if (v.g.vs_battle_pfileBuf().* == null) {
        v.g.vs_battle_pfileBuf().* = c.vs_main_allocHeapR(@as(c_uint, ctx.size));
    }
    return loadFile(@as(c_int, ctx.lba) +% 115009, @as(c_int, ctx.size), v.g.vs_battle_pfileBuf().*);
}
