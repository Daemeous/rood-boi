//! Chooses the marker id for an actor from its room or enemy class, then hands it to func_800AE4FC (src/BATTLE/BATTLE.PRG/44F14.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_44F14");
const c = @import("c_44F14");

/// orig: func_800AE828 (src/BATTLE/BATTLE.PRG/44F14.c)
/// Picks the id for an actor's marker from its room or enemy class, adjusts it by the variant, and draws it through func_800AE4FC.
pub fn func_800AE828(arg0: c_int, arg1: ?*v.D_800F4538_t, arg2: c_int) callconv(.c) void {
    const a = arg1.?;

    if (psx.conv(u8, @as(c_int, a.animationId) -% 0xCA) < 18) {
        return;
    }
    if (arg0 == 1) {
        return;
    }

    var id: c_int = undefined;
    if (psx.getBits(a, "unk5AC_28") != 0) {
        // The int at 0x5AC (the C's *((int*)&unk5B4 - 2)): its top nibble is unk5AC_28.
        const k = c.func_800A91DC(a.unk0.currentTileX, a.unk0.currentTileZ, 1);
        const e = psx.at(v.g.D_800F45E0(), k).*.?;
        const actorId = psx.getBits(&e.unk6C[8], "actorId");
        id = @as(c_int, v.g.D_800E91A4()[actorId]);
    } else {
        var floor: c_int = a.unk0.unk5D;
        id = 0xD;
        if (a.unk6E4 != 0) {
            floor = a.unk6E4;
        }
        const room = c.func_8008B764(a.unk0.currentTileX, a.unk0.currentTileZ, floor);
        if (room) |r| {
            const rv: *v._mpdRoomSection3 = @ptrCast(r);
            id = @as(c_int, v.g.D_800E9278()[psx.getBits(rv, "unk0_0")]);
        }
    }

    if (arg2 == 0x41) {
        id +%= 0x10;
    } else if (arg2 == 0x42) {
        id +%= 0x18;
    } else {
        var sp10: c_int = undefined;
        var sp14: c_int = undefined;
        c.func_8008EB04(&sp10, &sp14);
        if (sp14 >= 6 and sp10 > 0 and sp10 < 3) {
            id +%= 8;
        }
    }

    c.func_800AE4FC(@ptrCast(&a.unk0), id);
}
