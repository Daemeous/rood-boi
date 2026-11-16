//! Claims the battle flag byte vs_content_overlayWorkspaceOwner (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_8007E180 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sets the flag byte vs_content_overlayWorkspaceOwner to arg0 and returns 1 if it was clear; otherwise returns 0.
pub fn func_8007E180(arg0: c_int) callconv(.c) c_int {
    if (@as(c_int, v.g.vs_content_overlayWorkspaceOwner().*) == 0) {
        v.g.vs_content_overlayWorkspaceOwner().* = psx.conv(u8, arg0);
        return 1;
    }
    c.vs_main_nop9(0x66, 0);
    return 0;
}
