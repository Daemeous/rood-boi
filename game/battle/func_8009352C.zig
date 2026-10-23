//! Passes the room's section 14 buffer to the battle section loader, then records the count
//! (src/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_8009352C (src/BATTLE/BATTLE.PRG/2842C.c)
/// Runs the section 14 loader with the count from arg1 and stores that count in D_800F2258 when section 14 is present.
pub fn func_8009352C(_: ?*v.func_80092F74_t, arg1: ?*v.func_80092F74_t2) callconv(.c) c_int {
    if (psx.get(v.g.vs_battle_roomData().section14)) |section| {
        if (arg1.?.unk4[0].u16[0] == 0 or v.g.D_800F1D00().* != 0) {
            c.func_800462E8(0x200, @ptrCast(section), @as(c_uint, arg1.?.unk4[0].u16[1]));
        } else {
            c.func_800461CC(0x200, @ptrCast(section), @as(c_uint, arg1.?.unk4[0].u16[1]), 0x80, 0);
        }
        v.g.D_800F2258().* = psx.conv(i16, arg1.?.unk4[0].u16[1]);
    }
    return 0;
}
