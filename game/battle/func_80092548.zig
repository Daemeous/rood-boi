//! Room data's texture-effect and section D records: fills the entry table at D_800F1DD8 (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// The size of a section D record at `addr`: its length field, or 4 for a record whose second short is 11.
fn sectionDRecordSize(addr: usize) c_int {
    const hw: *align(1) [2]i16 = @ptrFromInt(addr);
    return if (hw[1] == 11) 4 else @as(c_int, hw[0]);
}

/// orig: func_80092548 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Walks the room's texture-effect and section D records, filling the entry table and calling each active entry's setup.
pub fn func_80092548() callconv(.c) void {
    const sizes = [7]c_int{ 24, 20, 32, 0, 0, 0, 36 };
    const room = v.g.vs_battle_roomData();

    v.g.D_800F2258().* = -1;
    v.g.D_800F1D00().* = 0;
    v.g.D_800F225A().* = 0;
    if (psx.conv(c_int, room.header.textureEffectsSectionLen) > 0) {
        var data: usize = @as(usize, psx.conv(u32, room.textureEffectsSection));
        const total: c_int = psx.conv(c_int, room.header.textureEffectsSectionLen);
        var consumed: c_int = 0;
        var index: c_int = 0;
        while (consumed < total) {
            const rec: *align(1) v.D_800F1DD8_t2 = @ptrFromInt(data);
            if (rec.unk6 < 7 and sizes[rec.unk6] != 0) {
                const entry = psx.at(v.g.D_800F1DD8(), index);
                psx.put(&entry.unk4, @as(*align(1) v.D_800F1DD8_t2, @ptrFromInt(data)));
                entry.unk0 = @intFromBool(rec.unk5 == 0);
                entry.unk2 = 1;
                entry.unk3 = 0;
                entry.unk1 = rec.unk6;
                rec.unk4 = 1;
                const step: c_int = sizes[rec.unk6];
                consumed +%= step;
                data +%= @as(usize, @bitCast(@as(isize, step)));
                index +%= 1;
            } else {
                consumed = total;
            }
        }
        v.g.D_800F225A().* = psx.conv(u8, index);
    }

    v.g.D_800F225B().* = 0;
    if (psx.conv(c_int, room.header.sectionDLen) > 0) {
        var data: usize = @as(usize, psx.conv(u32, room.sectionD));
        const total: c_int = psx.conv(c_int, room.header.sectionDLen);
        var offset: c_int = 0;
        var count: c_int = 64;
        while (offset < total) {
            const entry = psx.at(v.g.D_800F1DD8(), count);
            const rec: *align(1) v.func_80092548_t = @ptrFromInt(data);
            entry.unk0 = rec.active;
            entry.unk1 = rec.count;
            entry.unk2 = 1;
            entry.unk3 = 0;
            psx.put(&entry.unk4, @as(*align(1) v.D_800F1DD8_t2, @ptrFromInt(data)));
            psx.put(&entry.unk8, @as(*align(1) v.D_800F1DD8_t2, @ptrFromInt(data)));
            if (entry.unk0 != 0) {
                c.func_800927AC(psx.cptr(c.D_800F1DD8_t, entry));
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
            v.g.D_800F225B().* +%= 1;
            count +%= 1;
        }
    }
}
