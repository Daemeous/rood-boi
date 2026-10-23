//! Clears the room's section-14 pointer case: calls the host helper on it and sets D_800F2258 to -1 (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_800935B8 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Calls func_8004644C on the room's section 14 when it is set, then sets D_800F2258 to -1.
pub fn func_800935B8(_: ?*v.func_80092F74_t, arg1: ?*v.func_80092F74_t2) callconv(.c) c_int {
    const section14 = psx.get(v.g.vs_battle_roomData().section14);
    if (section14) |p| {
        c.func_8004644C(0x200, @ptrCast(p), @as(c_int, arg1.?.unk4[0].u16[1]));
        v.g.D_800F2258().* = -1;
    }
    return 0;
}
