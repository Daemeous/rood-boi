//! Switches the room's lighting state: CLUT slot 2's colours from 0x30 and the lamps (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: D_80068BFC (func_8008E4DC's `static u_short const`, in .rodata: not compared state, PORTING.md §6.3).
const lit = [16]u16{ 0x10, 1, 0x865, 0xC66, 0x869, 0xCA9, 0x88D, 0x10AD, 0xCB1, 0x14D1, 0xCD5, 0x14F5, 0xCF9, 0x113A, 0x199B, 0x21FC };

/// orig: func_8008E4DC (src/BATTLE/BATTLE.PRG/146C.c)
/// When the state differs from D_800F1D6F: loads 16 colours into CLUT slot 2 at 0x30 (the lit table, or the
/// room's own at D_800F1BA8 + 0x230), calls func_80093914 for each section-12 entry with unk0_12 and
/// func_800936F8 for section B's unk2C when not negative, and flips D_800F1D6F.
pub fn func_8008E4DC(state: c_int) callconv(.c) void {
    if (@as(c_int, v.g.D_800F1D6F().*) != state) {
        if (state != 0) {
            c.vs_main_loadClut(&lit, 2, 0x30, 0x10);
        } else {
            c.vs_main_loadClut(psx.at(v.g.D_800F1BA8().*.?, 0x230), 2, 0x30, 0x10);
        }
        const room = v.g.vs_battle_roomData();
        var entry: [*c]v._mpdRoomSection12 = @ptrCast(psx.get(room.section12));
        const count = psx.conv(c_int, room.header.section12Len / 184);
        var i: c_int = 0;
        while (i < count) : (i +%= 1) {
            if (psx.getBits(&entry[0], "unk0_12") != 0) {
                c.func_80093914(psx.getBits(&entry[0], "unk0_0"));
            }
            entry += 1;
        }
        if (psx.get(room.sectionB) != null) {
            if (psx.get(room.sectionB).?.unk2C >= 0) {
                _ = c.func_800936F8(psx.get(room.sectionB).?.unk2C);
            }
        }
        v.g.D_800F1D6F().* = @intFromBool(v.g.D_800F1D6F().* == 0);
    }
}
