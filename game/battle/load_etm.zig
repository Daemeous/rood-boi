//! The ETM texture loader for battle objects: streams the file and uploads its image to VRAM (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: _loadEtm (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Steps the ETM texture load of object `objdata`: -1 while the file streams, -2/-3 on failure, 0 when done.
pub fn loadEtm(objdata: ?*v.vs_battle_objectData) callconv(.c) c_int {
    const state = v.g._loadEtmState().*;

    // case 2: free the buffer and finish.
    if (state == 2) {
        c.vs_main_freeHeapR(@ptrCast(v.g._etmData().*.? - 2));
        v.g._loadEtmState().* = 0;
        return 0;
    }

    // default: start the load, then fall through to case 1.
    if (state != 1) {
        v.g._loadEtmState().* = 1;
        const od = objdata.?;
        const sectorSize: c_int = @as(c_int, psx.at(v.g._etmFileSectorSizes(), od.modelId).*);
        if (sectorSize == 0) {
            v.g._loadEtmState().* = 0;
            return -2;
        }

        // sectorSize is at most 255 here: the shift cannot overflow.
        const fileSize: c_int = sectorSize << 11;
        const data = c.vs_main_allocHeapR(psx.conv(c_uint, fileSize));
        v.g._etmData().* = @ptrFromInt(@intFromPtr(data));
        if (v.g._etmData().* == null) {
            return -2;
        }

        v.g._etmFile().lba = @as(c_int, psx.at(v.g._etmLbaOffsets(), od.modelId).*) +% 104351;
        v.g._etmFile().size = fileSize;
        const slot = c.vs_main_allocateCdQueueSlot(psx.cptr(c.vs_main_CdFile, v.g._etmFile()));
        v.g._etmFileCdSlot().* = @ptrCast(slot);
        c.vs_main_cdEnqueue(slot, data);
    }

    // case 1: wait for the file, then upload its image.
    const od = objdata.?;
    const slot = v.g._etmFileCdSlot().*.?;
    if (slot.state != 4) {
        return -1;
    }

    c.vs_main_freeCdQueueSlot(psx.cptr(c.vs_main_CdQueueSlot, slot));

    if (psx.at(v.g.D_800F244F(), od.index).* != 0) {
        if (c.func_8007E0A8(psx.conv(c_uint, psx.at(v.g.D_800F244F(), od.index).*), 1, 4) == 0) {
            return -3;
        }
        psx.at(v.g.D_800F244F(), od.index).* = 0;
    }

    if (c.func_8007DFF0(psx.conv(c_uint, od.unk11), 1, 4) == 0) {
        return -2;
    }

    psx.at(v.g.D_800F244F(), od.index).* = od.unk11;

    // ++_etmData; h = *_etmData; ++_etmData;
    v.g._etmData().* = v.g._etmData().*.? + 1;
    const h: c_int = v.g._etmData().*.?[0];
    v.g._etmData().* = v.g._etmData().*.? + 1;

    var rect: v.RECT = .{
        .x = psx.conv(i16, (@as(c_int, od.unk11) & 0xF) << 6),
        .y = 256,
        .w = 64,
        .h = psx.conv(i16, h),
    };
    _ = c.LoadImage(psx.cptr(c.RECT, &rect), @ptrFromInt(@intFromPtr(v.g._etmData().*.?)));

    v.g._loadEtmState().* = 2;
    return -1;
}
