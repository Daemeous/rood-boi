//! Finishes loading a zone file (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// `header + offset` as the C computes it (`offset + (long)header`); the file's offsets are word offsets.
fn part(comptime T: type, header: *v._zndHeader, offset: c_int) T {
    return @ptrFromInt(@intFromPtr(header) +% @as(usize, @bitCast(@as(isize, offset))));
}

/// orig: _finishLoadZnd (src/BATTLE/BATTLE.PRG/146C.c)
/// Queues zone file `id` unless one is queued, waits for it, frees its queue slot, hands its enemies, textures and
/// map list to their loaders, frees it, and switches the music to the zone's (when the music is on, D_800F18B0).
pub fn finishLoadZnd(id: c_int) callconv(.c) void {
    const zone = v.g._zoneContext();
    if (psx.get(zone.zndCdFile) == null) {
        loadZnd(id);
    }
    while (psx.get(zone.zndCdFile).?.state != c.vs_main_CdQueueStateLoaded) {
        _ = c.vs_main_gametimeUpdate(0);
    }
    c.vs_main_freeCdQueueSlot(@ptrCast(psx.get(zone.zndCdFile)));
    psx.put(&zone.zndCdFile, null);
    if (v.g.D_80050468().unk2 == 1) {
        c.func_8007BCCC();
    }
    const header: *v._zndHeader = @ptrCast(@alignCast(psx.get(zone.zndData).?));
    c._loadZndEnemies(part([*c]c_int, header, header.enemiesOffset), header.enemiesLen);
    c._loadZndTims(part(?*anyopaque, header, header.timOffset));
    c._loadMpdLbas(part(?*anyopaque, header, header.mpdOffset), header.mpdLen);
    const musicId = header.musicId;
    c.vs_main_freeHeapR(header);
    psx.put(&zone.zndData, null);
    if (v.g.vs_main_startState().* == 1) {
        c.func_800BEB9C(1);
    } else {
        c.func_800BEB9C(0);
    }
    if (c.vs_main_getCurrentMusicId() != musicId) {
        if (v.g.D_800F18B0().* != 0) {
            c.vs_main_stopMusic();
            var i: c_int = 0;
            while (i < 4) {
                i += 1; // i < 4
                _ = c.vs_main_freeMusic(i);
            }
            zone.musicSlot = 0;
            if (musicId != 0) {
                c.vs_main_loadAndWaitSoundSlot(musicId);
                zone.musicSlot = c.vs_main_loadAndWaitMusicSlot(musicId, 3);
            }
        }
    }
    zone.musicId = musicId;
    zone.unk30 = 1;
    zone.unk38 = 0;
}

/// orig: _loadZnd (src/BATTLE/BATTLE.PRG/146C.c)
/// Queues zone file `id` (vs_main_zndFiles) into a fresh heap block, the slot and block kept in _zoneContext
/// (vs_main_nop9 notes a slot or block still held); with music on (unk30), stops the current music when the
/// zone's (D_8004FCCC) differs. Static in the C: ported with each caller (PORTING.md §13).
fn loadZnd(id: c_int) void {
    const zone = v.g._zoneContext();
    const files = psx.global([256]v.vs_main_CdFile, "vs_main_zndFiles");
    var file: v.vs_main_CdFile = undefined;
    file.lba = psx.at(files, id).lba;
    file.size = psx.at(files, id).size;
    if (psx.get(zone.zndCdFile) != null) {
        c.vs_main_nop9(0x87, 0);
    }
    psx.put(&zone.zndCdFile, @as(?*v.vs_main_CdQueueSlot, @ptrCast(@alignCast(c.vs_main_allocateCdQueueSlot(@ptrCast(&file))))));
    if (psx.get(zone.zndData) != null) {
        c.vs_main_nop9(0x88, 0);
    }
    psx.put(&zone.zndData, c.vs_main_allocHeapR(psx.conv(c_uint, file.size)));
    c.vs_main_cdEnqueuePriority(@ptrCast(psx.get(zone.zndCdFile)), psx.get(zone.zndData));
    if (zone.unk30 != 0) {
        const music: c_int = psx.at(psx.globalMany(u8, "D_8004FCCC"), id).*;
        if (c.vs_main_getCurrentMusicId() != music) {
            _ = c.func_8004552C(v.g.vs_main_soundData().currentMusicSlot, 0, 0x78);
        }
    }
}
