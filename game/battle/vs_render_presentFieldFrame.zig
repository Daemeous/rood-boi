//! Frame end for the battle overlay: flips the frame buffer, reads the pads, presents the frame and flushes text (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_render_presentFieldFrame (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Ends a battle frame: swaps the buffers, holds on pause while polling pads, then draws the ordering table.
pub fn vs_render_presentFieldFrame(otag: [*c]c_uint) callconv(.c) c_int {
    v.g.vs_main_frameBuf().* = @intFromBool(v.g.vs_main_frameBuf().* == 0);
    _ = c.vs_main_getRand(0);
    c.SetGeomOffset(160, 112);
    _ = c.DrawSync(0);
    const ret: c_int = c.vs_main_gametimeUpdate(v.g.vs_gametime_tickspeed().*);
    if (v.g.vs_render_isFrameStepPaused().* != 0) {
        const pad = v.g.vs_main_padBuffer();
        const state = v.g.vs_main_buttonsState();
        const prev = v.g.vs_main_buttonsPreviousState();
        while (true) {
            _ = c.vs_main_gametimeUpdate(2);
            state.* = psx.conv(i32, psx.conv(u16, c.vs_main_updatePadState(0, @ptrCast(&pad[0]))));
            const pad1: c_int = c.vs_main_updatePadState(16, @ptrCast(&pad[1]));
            state.* |= pad1 << 16;
            c.vs_main_padConnect(0, @ptrCast(&pad[0]));
            c.vs_main_padConnect(16, @ptrCast(&pad[1]));
            v.g.vs_main_buttonsPressed().all = @bitCast(~prev.* & state.*);
            const temp_a0: c_uint = v.g.vs_main_buttonsPressed().all >> 16;
            v.g.vs_main_buttonsReleased().* = prev.* & ~state.*;
            prev.* = state.*;
            if ((temp_a0 & 0x800) != 0) {
                v.g.vs_render_isFrameStepPaused().* ^= 1;
            }
            if ((temp_a0 & 0x100) != 0 or v.g.vs_render_isFrameStepPaused().* == 0) {
                break;
            }
        }
    }
    const idx: usize = @intCast(v.g.vs_main_frameBuf().*);
    _ = c.PutDispEnv(psx.cptr(c.DISPENV, &v.g.vs_main_dispEnv()[idx]));
    _ = c.PutDrawEnv(psx.cptr(c.DRAWENV, &v.g.vs_main_drawEnv()[idx]));
    c.vs_main_commitClut();

    // ROOD_PC: HUD panels drawn into the ordering table (pc/src/hud).
    const HudPanels = *const fn (?*anyopaque) callconv(.c) void;
    if (psx.weak(HudPanels, "pc_hud_panels_classic")) |hud| hud(@ptrCast(otag));

    c.DrawOTag(otag);

    // ROOD_PC: frame-complete notification to the host.
    c.pc_battle_frame_complete();

    _ = c.FntFlush(-1);
    return ret;
}
