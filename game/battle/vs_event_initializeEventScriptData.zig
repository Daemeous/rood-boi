//! The event script's start (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// The u16 at byte `off` of `p` (the C's `*(u_short*)(p + off)`).
fn half(p: [*c]u8, off: usize) u16 {
    return psx.byteAt(u16, p, @intCast(off)).*;
}

/// orig: vs_event_initializeEventScriptData (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Opens an event script block: vs_event_dialogStrings and the string tables vs_event_textBoxFrameStyles[9..15] and
/// vs_event_vibrationPatterns[0..7] from its header's offsets (a zero entry: none), the main thread at data + 16
/// (the three others stopped), and the script state, text boxes and flags 16..31 and 168 reset.
pub fn vs_event_initializeEventScriptData(data: [*c]u8) callconv(.c) void {
    // (u_short*)(data + offset): stored by its bits, as the C does (an odd offset is not 2-aligned)
    @as(*usize, @ptrCast(v.g.vs_event_dialogStrings())).* = @intFromPtr(data + half(data, 2));
    var table = data + half(data, 4);
    var i: usize = 9;
    while (i < 16) : (i += 1) {
        const off = half(table, 2 * (i - 8));
        v.g.vs_event_textBoxFrameStyles()[i] = if (off != 0) table + off else null;
    }
    table = data + half(data, 6);
    i = 0;
    while (i < 8) : (i += 1) {
        const off = half(table, 2 * i);
        v.g.vs_event_vibrationPatterns()[i] = if (off != 0) table + off else null;
    }
    v.g.vs_event_threadCursors()[0] = data + 16;
    v.g.vs_event_threadCursors()[3] = null;
    v.g.vs_event_threadCursors()[2] = null;
    v.g.vs_event_threadCursors()[1] = null;
    v.g.vs_event_skipState().* = 0;
    v.g.vs_event_isSkipForced().* = 0;
    c.vs_battle_setStateFlag(168, 0);
    v.g.vs_event_threadWaitFrames()[3] = 0;
    v.g.vs_event_threadWaitFrames()[2] = 0;
    v.g.vs_event_threadWaitFrames()[1] = 0;
    v.g.vs_event_isEffectWaitPending().* = 0;
    v.g.vs_event_weaponTransitionPending().* = 0;
    v.g._textBoxSelectionPending().* = 0;
    v.g.vs_event_isScanlineOverlayEnabled().* = 0;
    v.g.vs_event_isMenuTransitionActive().* = 0;
    v.g.vs_event_roomChangeState().* = 0;
    v.g.vs_event_isFieldCameraRestoreScripted().* = 0;
    v.g.vs_event_threadWaitFrames()[0] = 0;
    v.g.vs_event_isMainThreadFinished().* = 0;
    i = 0;
    while (i < 8) : (i += 1) v.g.vs_battle_textBoxStatuses()[i] = 0;
    const pairs: [*]u8 = @ptrCast(v.g.vs_event_customEasingCurves()); // (scriptBytePair*)vs_event_customEasingCurves: {low, high}
    i = 0;
    while (i < 4) : (i += 1) {
        pairs[2 * i + 1] = 0;
        pairs[2 * i] = 0;
    }
    v.g.vs_event_weatherTweenDuration().* = 0;
    v.g.vs_event_splashTweens()[2].unk3 = 0;
    v.g.vs_event_splashTweens()[1].unk3 = 0;
    v.g.vs_event_splashTweens()[0].unk3 = 0;
    v.g.vs_event_musicSlot().* = psx.conv(u8, c.vs_main_getcurrentMusicSlot());
    v.g.vs_event_eventFileDiscRequest().* = null;
    var flag: c_short = 16;
    while (flag < 32) : (flag += 1) c.vs_battle_setStateFlag(flag, 0);
}
