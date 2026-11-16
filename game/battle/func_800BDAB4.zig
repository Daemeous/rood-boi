//! Passes the four flagged battle camera records to their screen-effect setters
//! (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BDAB4 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Sends each flagged record of the battle camera block to its screen-effect setter.
pub fn func_800BDAB4() callconv(.c) void {
    // sp10 and sp20 are uninitialised in the original: the callees copy the whole struct, but only the fields written here are read.
    var sp10: v.D_800F1A68_t = std.mem.zeroes(v.D_800F1A68_t);
    var sp20: v.P_CODE = std.mem.zeroes(v.P_CODE);

    if (c.func_800BDBB4(psx.cptr(c.func_800BDBB4_t, &v.g.vs_event_camera().*.?.screenScale)) != 0) {
        sp10.x = psx.conv(i32, v.g.vs_event_camera().*.?.screenScale.currentValue0);
        sp10.y = psx.conv(i32, v.g.vs_event_camera().*.?.screenScale.currentValue1);
        c.vs_render_setFramebufferEffectScale(psx.cptr(c.D_800F1A68_t, &sp10));
    }
    if (c.func_800BDBB4(psx.cptr(c.func_800BDBB4_t, &v.g.vs_event_camera().*.?.screenTint)) != 0) {
        sp20.r0 = psx.conv(u8, v.g.vs_event_camera().*.?.screenTint.currentValue0);
        sp20.g0 = psx.conv(u8, v.g.vs_event_camera().*.?.screenTint.currentValue1);
        sp20.b0 = psx.conv(u8, v.g.vs_event_camera().*.?.screenTint.currentValue2);
        c.vs_render_setFramebufferEffectTint(psx.cptr(c.P_CODE, &sp20));
    }
    if (c.func_800BDBB4(psx.cptr(c.func_800BDBB4_t, &v.g.vs_event_camera().*.?.screenTranslation)) != 0) {
        sp10.x = psx.conv(i32, v.g.vs_event_camera().*.?.screenTranslation.currentValue0);
        sp10.y = psx.conv(i32, v.g.vs_event_camera().*.?.screenTranslation.currentValue1);
        c.vs_render_setFramebufferEffectOffset(psx.cptr(c.D_800F1A68_t, &sp10));
    }
    if (c.func_800BDBB4(psx.cptr(c.func_800BDBB4_t, &v.g.vs_event_camera().*.?.unk25C)) != 0) {
        c.pc_screff2_set_warp(
            @as(c_int, v.g.vs_event_camera().*.?.unk25C.currentValue0),
            @as(c_int, v.g.vs_event_camera().*.?.unk25C.currentValue1),
        );
    }
}
