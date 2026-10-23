//! Clears the room's flag and, when the room has a section 14 and a valid
//! index in D_800F2258, calls the section-14 handler (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_80093A14 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Sets D_800F1D00 to 1, then runs the room's section-14 handler when its index is not -1 and the section exists.
pub fn func_80093A14() callconv(.c) void {
    v.g.D_800F1D00().* = 1;
    if (v.g.D_800F2258().* != -1) {
        if (psx.get(v.g.vs_battle_roomData().section14)) |section14| {
            c.func_80046494(0x200, @ptrCast(section14), @as(c_int, v.g.D_800F2258().*), 0x7F, 0);
        }
    }
}
