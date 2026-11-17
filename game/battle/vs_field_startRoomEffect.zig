//! Retires a slot of the vs_field_roomEffectInstances table, freeing the block it holds (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: vs_field_startRoomEffect (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Marks table slot `arg0` in use; for slots from 64 up to the limit, frees the held block and takes over the spare one.
pub fn vs_field_startRoomEffect(arg0: c_int) callconv(.c) c_int {
    if (arg0 < @as(c_int, v.g.vs_field_textureEffectCount().*)) {
        const entry = psx.at(v.g.vs_field_roomEffectInstances(), arg0);
        entry.unk0 = 1;
        return 1;
    }
    if (arg0 < 0x40) {
        return 0;
    }
    if (arg0 < @as(c_int, v.g.vs_field_roomEffectScriptCount().*) +% 0x40) {
        const entry = psx.at(v.g.vs_field_roomEffectInstances(), arg0);
        if (entry.unk0 != 0 or psx.get(entry.unk4) != psx.get(entry.unk8)) {
            c.vs_main_freeHeap(psx.get(entry.unk4));
        }
        entry.unk0 = 1;
        psx.put(&entry.unk4, psx.get(entry.unk8));
        c.vs_field_createRoomEffectObjects(psx.cptr(c.vs_field_RoomEffectInstance, entry));
        return 1;
    }
    return 0;
}
