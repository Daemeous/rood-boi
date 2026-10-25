//! Loads a weapon model file, or reuses one already loaded
//! (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: _loadWep (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Finds the weapon model among the loaded ones or starts its CD read; returns -1 while the read is pending, else the parse result.
pub fn loadWep(arg0: ?*v.vs_battle_objectData) callconv(.c) c_int {
    const obj = arg0.?;
    if (obj.modelId == 0 or obj.modelId >= 0x80) {
        return 0;
    }

    // switch (_loadShpState): 2 breaks to the parse, 1 enters case 1, 0 searches
    // and falls through to the allocation, and any other value takes default.
    const state = v.g._loadShpState().*;
    var skipToParse = state == 2;
    if (!skipToParse and state != 1) {
        if (state == 0) {
            var found = false;
            const wm = v.g.vs_battle_wepModels();
            var i: c_int = 0;
            while (i < 20) : (i +%= 1) {
                const w = wm[@intCast(i)];
                if (i != @as(c_int, obj.index) and w != null and
                    (psx.getBits(w.?, "actorId") != 0 or @as(c_int, obj.index) < 4) and
                    @as(c_int, w.?.modelId) == @as(c_int, obj.modelId))
                {
                    obj.dataAddr = psx.conv(u32, i);
                    found = true;
                    break;
                }
            }
            if (found) {
                v.g._wepFileLoaded().* = 0;
                skipToParse = true;
            } else {
                v.g._wepFileLoaded().* = 1;
            }
        }
        if (!skipToParse) {
            // default: start the read of the model file.
            var wepFileSize: c_int = @as(c_int, psx.at(v.g._wepFileSectorOffsets(), obj.modelId).*);
            wepFileSize = wepFileSize << 11;
            v.g._wepFileData().* = c.vs_main_allocHeapR(psx.conv(c_uint, wepFileSize));
            obj.dataAddr = psx.addr(v.g._wepFileData().*); // psx_to_int: fatal above 4 GB

            if (v.g._wepFileData().* == null) {
                return -2;
            }

            v.g._loadShpState().* = 1;
            v.g._wepFile().lba = @as(c_int, psx.at(v.g.D_800E8D00(), obj.modelId).*) +% 104024;
            v.g._wepFile().size = wepFileSize;
            v.g._wepCdSlot().* = @ptrCast(c.vs_main_allocateCdQueueSlot(psx.cptr(c.vs_main_CdFile, v.g._wepFile())));
            c.vs_main_cdEnqueue(@ptrCast(v.g._wepCdSlot().*), @as(?*anyopaque, @ptrFromInt(@as(usize, obj.dataAddr))));
        }
    }

    if (!skipToParse) {
        // case 1: wait for the read; once it is done, release its slot and move on.
        const slot = v.g._wepCdSlot().*.?;
        if (@as(c_int, slot.state) != 4) {
            return -1;
        }
        c.vs_main_freeCdQueueSlot(@ptrCast(v.g._wepCdSlot().*));
        v.g._loadShpState().* = 2;
        return -1;
    }

    const wepData = c._parseWep(@ptrCast(obj));

    if (wepData == -1) {
        return -1;
    }

    if (v.g._wepFileLoaded().* != 0) {
        v.g._wepFileLoaded().* = 0;
        c.vs_main_freeHeapR(v.g._wepFileData().*);
    }

    v.g._loadShpState().* = 0;
    return wepData;
}
