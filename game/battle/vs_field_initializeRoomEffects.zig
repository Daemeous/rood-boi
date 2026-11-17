//! Room data's texture-effect and section D records: fills the entry table at vs_field_roomEffectInstances (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// The size of a section D record at `addr`: its length field, or 4 for a record whose second short is 11.
fn sectionDRecordSize(addr: usize) c_int {
    const hw: *align(1) [2]i16 = @ptrFromInt(addr);
    return if (hw[1] == 11) 4 else @as(c_int, hw[0]);
}

/// orig: vs_field_initializeRoomEffects (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Walks the room's texture-effect and section D records, filling the entry table and calling each active entry's setup.
pub fn vs_field_initializeRoomEffects() callconv(.c) void {
    const sizes = [7]c_int{ 24, 20, 32, 0, 0, 0, 36 };
    const room = v.g.vs_battle_roomData();

    v.g.vs_field_roomEffectSoundId().* = -1;
    v.g.vs_field_isRoomEffectSoundRestored().* = 0;
    v.g.vs_field_textureEffectCount().* = 0;
    if (psx.conv(c_int, room.header.textureEffectsSectionLen) > 0) {
        var data: usize = @intFromPtr(room.textureEffectsSection);
        const total: c_int = psx.conv(c_int, room.header.textureEffectsSectionLen);
        var consumed: c_int = 0;
        var index: c_int = 0;
        while (consumed < total) {
            const rec: *align(1) v.vs_field_SwingEffectRecord = @ptrFromInt(data);
            if (rec.kind < 7 and sizes[rec.kind] != 0) {
                const entry = psx.at(v.g.vs_field_roomEffectInstances(), index);
                psx.put(&entry.unk4, @as(*align(1) v.vs_field_SwingEffectRecord, @ptrFromInt(data)));
                entry.unk0 = @intFromBool(rec.startsInactive == 0);
                entry.unk2 = 1;
                entry.unk3 = 0;
                entry.unk1 = rec.kind;
                rec.needsReset = 1;
                const step: c_int = sizes[rec.kind];
                consumed +%= step;
                data +%= @as(usize, @bitCast(@as(isize, step)));
                index +%= 1;
            } else {
                consumed = total;
            }
        }
        v.g.vs_field_textureEffectCount().* = psx.conv(u8, index);
    }

    v.g.vs_field_roomEffectScriptCount().* = 0;
    if (psx.conv(c_int, room.header.sectionDLen) > 0) {
        var data: usize = @intFromPtr(room.sectionD);
        const total: c_int = psx.conv(c_int, room.header.sectionDLen);
        var offset: c_int = 0;
        var count: c_int = 64;
        while (offset < total) {
            const entry = psx.at(v.g.vs_field_roomEffectInstances(), count);
            const rec: *align(1) v.func_80092548_t = @ptrFromInt(data);
            entry.unk0 = rec.active;
            entry.unk1 = rec.count;
            entry.unk2 = 1;
            entry.unk3 = 0;
            psx.put(&entry.unk4, @as(*align(1) v.vs_field_SwingEffectRecord, @ptrFromInt(data)));
            psx.put(&entry.unk8, @as(*align(1) v.vs_field_SwingEffectRecord, @ptrFromInt(data)));
            if (entry.unk0 != 0) {
                c.vs_field_createRoomEffectObjects(psx.cptr(c.vs_field_RoomEffectInstance, entry));
            }

            var size: c_int = @as(c_int, entry.unk1) +% 4;
            if ((entry.unk1 & 3) != 0) {
                const alignedSize: c_int = @as(c_int, entry.unk1) +% 8;
                size = alignedSize -% (@as(c_int, entry.unk1) & 3);
            }
            data +%= @as(usize, @bitCast(@as(isize, size)));
            offset +%= size;

            size = sectionDRecordSize(data);
            while (size != 0) {
                data +%= @as(usize, @bitCast(@as(isize, size)));
                offset +%= size;
                size = sectionDRecordSize(data);
            }
            offset +%= 4;
            data +%= 4;
            v.g.vs_field_roomEffectScriptCount().* +%= 1;
            count +%= 1;
        }
    }
}
