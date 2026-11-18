//! Finishes loading a zone file (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: _finishLoadZnd (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Queues zone file `id` unless one is queued, waits for it, frees its queue slot, hands its enemies, textures and
/// map list to their loaders, frees it, and switches the music to the zone's (when the music is on, vs_audio_zoneMusicSwitchEnabled).
pub fn finishLoadZnd(id: c_int) callconv(.c) void {
    const zone = v.g._zoneContext();
    if (psx.get(zone.zndCdFile) == null) {
        loadZnd(id);
    }
    while (psx.get(zone.zndCdFile).?.state != c.vs_main_CdQueueStateLoaded) {
        _ = c.vs_main_gametimeUpdate(0);
    }
    c.vs_main_freeCdQueueSlot(@ptrCast(@alignCast(psx.get(zone.zndCdFile))));
    psx.put(&zone.zndCdFile, null);
    if (v.g.vs_state_sceneTimer().resetScope == 1) {
        c.vs_state_resetSceneTimer();
    }
    const header = psx.get(zone.zndData).?;
    var znd: c.vs_znd_file = undefined;
    c.vs_znd_decode(&znd, header);
    c._loadZndEnemies(@ptrCast(@alignCast(znd.enemies.data)), znd.enemies.size);
    c._loadZndTims(znd.tims.data);
    c._loadMpdLbas(znd.mpd.data, znd.mpd.size);
    const musicId = znd.musicId;
    c.vs_main_freeHeapR(header);
    psx.put(&zone.zndData, null);
    if (v.g.vs_main_startState().* == 1) {
        c.vs_state_scriptFlagsResetZone(1);
    } else {
        c.vs_state_scriptFlagsResetZone(0);
    }
    if (c.vs_main_getCurrentMusicId() != musicId) {
        if (v.g._zoneContext().unk30 != 0) {
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

/// orig: _loadZnd (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Queues zone file `id` (vs_main_zndFiles) into a fresh heap block, the slot and block kept in _zoneContext
/// (vs_main_nop9 notes a slot or block still held); with music on (unk30), stops the current music when the
/// zone's (vs_field_zoneMusicIds) differs. Static in the C: ported with each caller (PORTING.md §13).
fn loadZnd(id: c_int) void {
    const zone = v.g._zoneContext();
    const files = psx.Global([256]v.vs_main_CdFile, "vs_main_zndFiles").ptr();
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
    c.vs_main_cdEnqueuePriority(@ptrCast(@alignCast(psx.get(zone.zndCdFile))), psx.get(zone.zndData));
    if (zone.unk30 != 0) {
        const music: c_int = psx.at(psx.GlobalMany(u8, "vs_field_zoneMusicIds").ptr(), id).*;
        if (c.vs_main_getCurrentMusicId() != music) {
            _ = c.vs_audio_fadeMusicSlotVolume(v.g.vs_main_soundData().currentMusicSlot, 0, 0x78);
        }
    }
}
