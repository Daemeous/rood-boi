//! A warp to a location (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_8007C694 (src/BATTLE/BATTLE.PRG/146C.c)
/// Sets up D_800F1AB0 as the door into location `location` (arg1, arg2 into its unk4_8, unk6_10; actor 0's facing
/// rounded to a quarter turn into unk4_13), starts the screen transition with the given effect and speed, and
/// queues the zone file when the zone changes (step 2) or else the map file when the map changes (step 1).
pub fn func_8007C694(location: c_int, arg1: c_int, arg2: c_int, effect: c_int, speed: c_int) callconv(.c) c_int {
    var actor: v.func_8006EBF8_t = undefined;
    const ids = v.g.vs_main_locationZoneMapIds();
    const zndId: c_int = psx.at(ids, location *% 2).*;
    const mpdId: c_int = psx.at(ids, location *% 2 +% 1).*;
    const door = v.g.D_800F1AB0();
    psx.setBits(door, "unk4_8", psx.conv(u5, arg1));
    psx.setBits(door, "unk6_10", psx.conv(u5, arg2));
    psx.setBits(&door.unk2, "unk0_13", 0);
    _ = c.func_800A1108(0, &actor);
    const facing: c_int = @as(c_int, actor.unk0.unk4.pad) +% 0x200;
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
    c.func_8006C250();
    c.func_800A0A1C(0, 1);
    c.func_8008B8F8(null);
    return 1;
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
