//! Switches the room's lighting state: CLUT slot 2's colours from 0x30 and the lamps (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_platformFreezePalette (vs_field_setPlatformFreezeAppearance's `static u_short const`, in .rodata: not compared state, PORTING.md §6.3).
const lit = [16]u16{ 0x10, 1, 0x865, 0xC66, 0x869, 0xCA9, 0x88D, 0x10AD, 0xCB1, 0x14D1, 0xCD5, 0x14F5, 0xCF9, 0x113A, 0x199B, 0x21FC };

/// orig: vs_field_setPlatformFreezeAppearance (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// When the state differs from vs_field_isPlatformFreezeTintActive: loads 16 colours into CLUT slot 2 at 0x30 (the lit table, or the
/// room's own at vs_field_roomPalettes + 0x230), calls vs_field_swapRoomGeometryRedGreen for each section-12 entry with unk0_12 and
/// vs_field_toggleRoomEffectUpdates for section B's unk2C when not negative, and flips vs_field_isPlatformFreezeTintActive.
pub fn vs_field_setPlatformFreezeAppearance(state: c_int) callconv(.c) void {
    if (@as(c_int, v.g.vs_field_isPlatformFreezeTintActive().*) != state) {
        if (state != 0) {
            c.vs_main_loadClut(&lit, 2, 0x30, 0x10);
        } else {
            c.vs_main_loadClut(psx.at(v.g.vs_field_roomPalettes().*.?, 0x230), 2, 0x30, 0x10);
        }
        const room = v.g.vs_battle_roomData();
        var entry: [*c]v._mpdRoomSection12 = @ptrCast(psx.get(room.section12));
        const count = psx.conv(c_int, room.header.section12Len / 184);
        var i: c_int = 0;
        while (i < count) : (i +%= 1) {
            if (psx.getBits(&entry[0], "unk0_12") != 0) {
                c.vs_field_swapRoomGeometryRedGreen(psx.getBits(&entry[0], "unk0_0"));
            }
            entry += 1;
        }
        if (psx.get(room.sectionB) != null) {
            if (psx.get(room.sectionB).?.unk2C >= 0) {
                _ = c.vs_field_toggleRoomEffectUpdates(psx.get(room.sectionB).?.unk2C);
            }
        }
        v.g.vs_field_isPlatformFreezeTintActive().* = @intFromBool(v.g.vs_field_isPlatformFreezeTintActive().* == 0);
    }
}
