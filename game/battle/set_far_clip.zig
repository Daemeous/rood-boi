//! Camera far clip and draw distance (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_setFarClip (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Stores the far clip in the scratchpad camera and, when arg1 is 0, passes it on as a draw distance in 8-unit steps, clamped to 0..0x800.
pub fn setFarClip(farClip: c_int, arg1: c_int) callconv(.c) void {
    var clip: c_int = farClip;
    v.g.vs_view().camera.farClip = clip;

    if (arg1 == 0) {
        if (clip < 0) {
            clip +%= 7;
        }
        clip >>= 3;
        if (clip > 0x800) {
            clip = 0x800;
        } else if (clip < 0) {
            clip = 0;
        }
        c.vs_render_setRoomTextureLodDepth(clip);
    }
}
