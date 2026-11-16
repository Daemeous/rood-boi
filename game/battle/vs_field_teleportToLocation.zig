//! A warp to a location (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_teleportToLocation (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sets up vs_field_pendingDestinationDoor as the door into location `location` (arg1, arg2 into its unk4_8, unk6_10; actor 0's facing
/// rounded to a quarter turn into unk4_13), starts the screen transition with the given effect and speed, and
/// queues the zone file when the zone changes (step 2) or else the map file when the map changes (step 1).
pub fn vs_field_teleportToLocation(location: c_int, arg1: c_int, arg2: c_int, effect: c_int, speed: c_int) callconv(.c) c_int {
    var actor: v.func_8006EBF8_t = undefined;
    const ids = v.g.vs_main_locationZoneMapIds();
    const zndId: c_int = psx.at(ids, location *% 2).*;
    const mpdId: c_int = psx.at(ids, location *% 2 +% 1).*;
    const door = v.g.vs_field_pendingDestinationDoor();
    psx.setBits(door, "unk4_8", psx.conv(u5, arg1));
    psx.setBits(door, "unk6_10", psx.conv(u5, arg2));
    psx.setBits(&door.tileLocation, "unk0_13", 0);
    _ = c.vs_actors_actorPlacement(0, &actor);
    const facing: c_int = @as(c_int, actor.location.position.pad) +% 0x200;
    var quarter = facing -% @divTrunc(facing, 4096) *% 4096;
    if (quarter < 0) {
        quarter +%= 0x3FF;
    }
    v.g.vs_battle_screenTransitionStep().* = 1;
    v.g.vs_battle_screenTransitionEffect().* = effect;
    v.g.vs_battle_screenTransitionSpeed().* = speed;
    psx.setBits(door, "unk4_13", psx.conv(u2, quarter >> 10));
    psx.setBits(door, "unk6_15", 0);
    if (zndId != door.zndId) {
        v.g.vs_battle_screenTransitionStep().* = 2;
        loadZnd(zndId);
    } else if (mpdId != door.mpdId) {
        v.g.vs_battle_screenTransitionStep().* = 1;
        c._loadMpd(mpdId);
    }
    door.zndId = psx.conv(u8, zndId);
    door.mpdId = psx.conv(u8, mpdId);
    c.vs_actors_freezeAllActorAnimations();
    c.vs_field_setActorCollisionBypass(0, 1);
    c.vs_field_setEntranceDoorRecord(null);
    return 1;
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
