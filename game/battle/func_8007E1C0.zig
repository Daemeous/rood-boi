//! Consumes the pending flag at vs_content_overlayWorkspaceOwner when it matches the argument
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_8007E1C0 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Clears vs_content_overlayWorkspaceOwner and returns 1 when it equals arg0; otherwise calls vs_main_nop9(0x67, 0) and returns 0.
pub fn func_8007E1C0(arg0: c_int) callconv(.c) c_int {
    const flag = v.g.vs_content_overlayWorkspaceOwner();
    if (@as(c_int, flag.*) == arg0) {
        flag.* = 0;
        return 1;
    }
    c.vs_main_nop9(0x67, 0);
    return 0;
}
