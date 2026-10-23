//! Battle unit 2842C: a conditional store of 0x100 through a guest pointer
//! (src/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");

/// orig: func_80092F74 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Stores 0x100 at the pointer in `arg0.unk0` when `arg0.unk8` is non-negative; returns 0.
pub fn func_80092F74(arg0: ?*v.func_80092F74_t, _: ?*v.func_80092F74_t2) callconv(.c) c_int {
    if (arg0.?.unk8 >= 0) {
        psx.get(arg0.?.unk0).?.* = 0x100;
    }
    return 0;
}
