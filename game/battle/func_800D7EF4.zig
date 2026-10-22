//! The battle overlay's PLG load step: starts the load of the PLG file into overlay slot 3 and completes it (src/BATTLE/BATTLE.PRG/6F014.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_6F014");
const c = @import("c_6F014");

/// orig: _loadfile (src/BATTLE/BATTLE.PRG/6F014.c)
/// Queues a CD read of `size` bytes at `lba` into `buf`; once it has loaded, frees the slot and returns 0, else 1.
fn loadFile(lba: c_int, size: c_int, buf: ?*anyopaque) c_int {
    var cdFile: c.vs_main_CdFile = undefined;
    const slot = psx.global(?*c.vs_main_CdQueueSlot, "D_800F568C");

    if (slot.* == null) {
        cdFile.lba = psx.conv(@TypeOf(cdFile.lba), lba);
        cdFile.size = psx.conv(@TypeOf(cdFile.size), size);
        slot.* = c.vs_main_allocateCdQueueSlot(&cdFile);
        _ = c.vs_main_cdEnqueue(slot.*, buf);
        return 1;
    }

    if (slot.*.?.state == c.vs_main_CdQueueStateLoaded) {
        _ = c.vs_main_freeCdQueueSlot(slot.*.?);
        slot.* = null;
        return 0;
    }

    return 1;
}

/// orig: func_800D7EF4 (src/BATTLE/BATTLE.PRG/6F014.c)
/// Advances the PLG load by one step: state 1 starts it, states 1 and 2 read the file, and a finished read moves the state to 3.
pub fn func_800D7EF4() callconv(.c) c_int {
    const state: c_uint = v.g._plgLoadState().*;
    // Uninitialised in the original for states other than 0..3; 0 here.
    var ret: c_int = 0;

    // Case 1 falls through to case 2 (written as two tests: a labelled `continue :sw`
    // emits a writable jump table in the engine library's .data).
    if (state == 1) {
        if (v.g.D_800F5684().* == 0) {
            _ = c.func_8007E180(2);
            v.g.D_800F5684().* = psx.conv(i32, state);
        }
    }
    if (state == 1 or state == 2) {
        const ctx = v.g._plgLoadContext();
        ret = loadFile(
            @as(c_int, ctx.lba) +% 115009,
            @as(c_int, ctx.size),
            v.g.vs_overlay_slots()[3],
        );
        if (ret == 0) {
            c.vs_battle_setPlgLoadState(3);
        }
    } else if (state == 0 or state == 3) {
        ret = 0;
    }

    return ret;
}
