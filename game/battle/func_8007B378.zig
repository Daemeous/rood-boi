//! Loads three consecutive CLUT transition slots from one source (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const c = @import("c_146C");

/// orig: func_8007B378 (src/BATTLE/BATTLE.PRG/146C.c)
/// Calls the CLUT loader for slots 0, 1 and 2, the source advanced by 0x100 u_shorts per slot. The C
/// passes arg3 only to the first call: the other two omit the fourth argument (a bug kept; the callee reads
/// whatever the register or stack slot holds). arg2 is unused.
pub fn func_8007B378(arg0: c_int, arg1: [*c]c_ushort, arg2: c_int, arg3: c_int) callconv(.c) void {
    _ = arg2;
    c.vs_main_loadClutTransition(arg0, @as(c_int, 0), arg1, arg3);
    c.vs_main_loadClutTransition(arg0, @as(c_int, 1), psx.at(arg1, 0x100));
    c.vs_main_loadClutTransition(arg0, @as(c_int, 2), psx.at(arg1, 0x200));
}
