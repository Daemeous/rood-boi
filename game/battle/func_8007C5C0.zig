//! Through a door into another zone (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_8007C5C0 (src/BATTLE/BATTLE.PRG/146C.c)
/// Keeps the door section in D_800F1AB0, starts screen transition step 2 with the given effect and speed, queues
/// the door's zone file (_loadZnd), and resets actor 0 (func_8009E5C4, func_800A0A1C) and the entered door.
pub fn func_8007C5C0(door: ?*v._mpdRoomDoorSection_t, effect: c_int, speed: c_int) callconv(.c) c_int {
    v.g.D_800F1AB0().* = door.?.*;
    v.g.vs_battle_screenTransitionStep().* = 2;
    v.g.vs_battle_screenTransitionEffect().* = effect;
    v.g.vs_battle_screenTransitionSpeed().* = speed;
    loadZnd(door.?.zndId);
    c.func_8009E5C4(0);
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
