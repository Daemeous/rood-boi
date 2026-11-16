//! Finds the room section-11 entry matching a request and plays its bytes
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_8008C2C0 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Finds the room section-11 entry for arg0..arg3 at the camera's facing, calls func_80091AC0 on each of its bytes and returns 1; returns 0 when none matches.
pub fn func_8008C2C0(arg0: c_int, arg1: c_int, arg2: c_int, arg3: c_int) callconv(.c) c_int {
    if (psx.get(v.g.vs_battle_roomData().section11) != null) {
        c.vs_field_clearGeometryOcclusion();
        const facing: c_int = ((v.g.vs_view().camera.angles.vy +% 0x900) & 0xFFF) >> 9;
        var entry: *align(1) v._mpdRoomSection11 = psx.get(v.g.vs_battle_roomData().section11).?;
        const len: c_int = psx.conv(c_int, v.g.vs_battle_roomData().header.section11Len);
        var i: c_int = 0;
        var j: c_int = 0;
        while (i < len) {
            const new_var = entry;
            if (@as(c_int, psx.getBits(new_var, "unk0_0")) == arg0) {
                if (@as(c_int, psx.getBits(new_var, "unk0_5")) == arg1 and
                    @as(c_int, psx.getBits(new_var, "unk0_10")) == arg2 and
                    @as(c_int, psx.getBits(new_var, "unk0_14")) == arg3 and
                    @as(c_int, psx.getBits(new_var, "unk0_11")) == facing)
                {
                    // unk4 is the start of the byte list; s1 advances one byte per j.
                    j = 0;
                    while (j < @as(c_int, new_var.effectCount)) : (j +%= 1) {
                        c.func_80091AC0(@as(c_int, psx.byteAt(u8, new_var, 4 + @as(isize, j)).*));
                    }
                    return 1;
                }
            }
            const unk2: c_int = new_var.effectCount;
            j = unk2 +% ((4 -% (unk2 & 3)) & 3) +% 4;
            i +%= j;
            entry = psx.byteAt(v._mpdRoomSection11, entry, @as(isize, j));
        }
    }
    return 0;
}
