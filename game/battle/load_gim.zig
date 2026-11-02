//! Loads a GIM image set (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_battle_loadGim (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Once (D_800EB9BC null): three image records for GIM `id` (palette arg1), a heap buffer of its size and a CD read of it into the buffer.
pub fn loadGim(id: c_int, palette: c_int) callconv(.c) void {
    if (v.g.D_800EB9BC().* == null) {
        var gim: [*]v.gim_t = @ptrCast(@alignCast(c.vs_main_allocHeapR(@sizeOf(v.gim_t) * 3).?));
        v.g.D_800EB9BC().* = &gim[0];
        const lbas = v.g._gimLbas();
        psx.put(&gim[0].data, @as(?*u16, @ptrCast(@alignCast(c.vs_main_allocHeapR(@bitCast(@as(c_int, psx.conv(u8, psx.at(lbas, id).*)) << 0xB))))));
        var i: c_int = 0;
        while (i < 3) : (i += 1) { // i < 3
            @as(*u16, @ptrCast(&gim[0])).* = 0;
            psx.setBits(&gim[0], "unk0_1", 1);
            psx.setBits(&gim[0], "unk0_6", 1);
            gim[0].unk2 = psx.conv(u8, palette);
            gim[0].unk3 = 0x80;
            gim[0].id = psx.conv(i16, id);
            gim[0].unk8 = 0;
            gim[0].unkA = 0;
            gim[0].unkC = 0x1000;
            gim += 1;
        }
        var file: v.vs_main_CdFile = undefined;
        file.lba = @bitCast(psx.at(lbas, id).* >> 8);
        file.size = @as(c_int, psx.conv(u8, psx.at(lbas, id).*)) << 0xB;
        const first = v.g.D_800EB9BC().*.?;
        psx.put(&first.cdQueueSlot, @as(?*v.vs_main_CdQueueSlot, @ptrCast(c.vs_main_allocateCdQueueSlot(@ptrCast(&file)))));
        c.vs_main_cdEnqueue(@ptrCast(psx.get(first.cdQueueSlot)), psx.get(first.data));
    }
}
