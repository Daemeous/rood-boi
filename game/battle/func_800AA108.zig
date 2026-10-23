//! Sets a battle slot's tile and facing from a position record, and its state
//! (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: func_800AA108 (src/BATTLE/BATTLE.PRG/40564.c)
/// Stores arg2 as the slot's state and moves the slot to the tile and facing in `fields`.
pub fn func_800AA108(arg0: c_int, arg1: ?*v.func_8006EBF8_t_fields, arg2: c_int, arg3: c_int) callconv(.c) void {
    const temp_s0 = psx.at(v.g.D_800F4538(), arg0).*;

    if (temp_s0) |s| {
        s.unk5C4 = psx.conv(u16, arg2);

        if (arg2 == 0 and arg3 == 0) {
            c.func_800AA290(arg0, @ptrCast(arg1), -1, 0);

            var temp_a1: c_int = @as(c_int, psx.getBits(arg1.?, "unk0_24"));
            temp_a1 *%= 0x10;
            temp_a1 = @rem(temp_a1, 0x1000);
            temp_a1 -%= @as(c_int, s.unk0.facing);

            c.func_800AA984(arg0, psx.conv(c_short, temp_a1), 0);
            psx.setBits(s, "unk5AC_0", 0);
        } else {
            c.func_800AA490(arg0, @ptrCast(arg1), arg3, @as(c_int, s.unk5C0));
            psx.setBits(s, "unk5AC_0", 1);
        }

        s.unk0.currentTileX = psx.getBits(arg1.?, "unk0_0");
        s.unk0.unk5D = psx.getBits(arg1.?, "unk0_8");
        s.unk0.currentTileZ = psx.getBits(arg1.?, "unk0_16");
        psx.setBits(s, "unk5AC_2", 1);
    }
}
