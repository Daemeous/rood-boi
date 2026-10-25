//! Shape-file loading for a battle object: shares a loaded copy, or reads the file from CD and parses it
//! (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// Looks for another loaded object (not this one) with the same model; on a hit sets dataAddr to its index.
fn findShared(obj: *v.vs_battle_objectData) bool {
    const table = v.g.D_800F4538();
    var i: c_int = 0;
    while (i < 17) : (i +%= 1) {
        if (i != @as(c_int, obj.index)) {
            const idx: usize = @intCast(i);
            if (table[idx]) |entry| {
                if (@as(c_int, entry.unk6E6) == @as(c_int, obj.modelId)) {
                    obj.dataAddr = psx.conv(u32, i);
                    return true;
                }
            }
        }
    }
    return false;
}

/// Allocates the shape buffer, queues its CD read and moves to the wait state; then checks the read.
fn startLoad(arg0: ?*v.vs_battle_objectData) c_int {
    const obj = arg0.?;
    const sizes = v.g._shpFileSectorSizes();
    var shpFileSize: c_int = @as(c_int, psx.at(sizes, obj.modelId).*);
    if (shpFileSize == 0) {
        obj.modelId = 0;
        shpFileSize = @as(c_int, sizes[0]);
    }
    shpFileSize <<= 11;

    const data = c.vs_main_allocHeapR(psx.conv(c_uint, shpFileSize));
    v.g._shpData().* = data;
    obj.dataAddr = if (data) |p| psx.addr(p) else 0;
    if (data == null) return -2;

    v.g._loadShpState().* = 1;
    const lbas = v.g._shpLbaOffsets();
    const lba: c_int = @as(c_int, psx.at(lbas, obj.modelId).*) +% 95000;
    v.g._shpFile().lba = lba;
    v.g._shpFile().size = shpFileSize;
    const slot = c.vs_main_allocateCdQueueSlot(psx.cptr(c.vs_main_CdFile, v.g._shpFile()));
    v.g._shpCdSlot().* = @ptrCast(slot);

    c.vs_main_cdEnqueue(slot, @ptrFromInt(@as(usize, obj.dataAddr)));
    return awaitCd();
}

/// Waits for the CD read to finish; when it has, frees the queue slot and moves to the parse state.
fn awaitCd() c_int {
    const slot = v.g._shpCdSlot().*.?;
    if (slot.state != 4) return -1;
    c.vs_main_freeCdQueueSlot(psx.cptr(c.vs_main_CdQueueSlot, slot));
    v.g._loadShpState().* = 2;
    return -1;
}

/// orig: _loadShp (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Loads a battle object's shape file: shares a loaded copy, or reads it from CD, then parses it.
pub fn loadShp(arg0: ?*v.vs_battle_objectData) callconv(.c) c_int {
    switch (v.g._loadShpState().*) {
        0 => {
            const obj = arg0.?;
            if (obj.modelId != 0x7F and !findShared(obj)) {
                v.g._shpAllocated().* = 1;
                return startLoad(arg0);
            }
            v.g._shpAllocated().* = 0;
        },
        1 => return awaitCd(),
        2 => {},
        else => return startLoad(arg0),
    }

    const shpData = c._parseShp(@ptrCast(arg0));
    if (shpData == -1) return -1;

    if (v.g._shpAllocated().* != 0) {
        v.g._shpAllocated().* = 0;
        c.vs_main_freeHeapR(v.g._shpData().*);
    }

    v.g._loadShpState().* = 0;
    return shpData;
}
