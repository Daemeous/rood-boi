//! Stores a time value (a frame count packed into bytes) in vs_state_sceneTimer and pushes its four bytes to the state flags (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_state_startSceneTimer (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Records arg0 and arg1, splits arg2 into frames, seconds, minutes and hours unless arg0 is 1, and sets state flags 0xA0..0xA3.
pub fn vs_state_startSceneTimer(arg0: c_int, arg1: c_int, arg2: c_int) callconv(.c) void {
    const st = v.g.vs_state_sceneTimer();
    st.mode = psx.conv(u8, arg0);
    st.resetScope = psx.conv(u16, arg1);
    st.hours = 0;
    st.minutes = 0;
    st.seconds = 0;
    st.frames = 0;

    if (arg0 != 1) {
        st.frames = psx.conv(i8, @rem(arg2, 256));
        st.seconds = psx.conv(i8, @rem(@divTrunc(arg2, 256), 256));
        st.minutes = psx.conv(i8, @rem(@divTrunc(arg2, 65536), 256));
        st.hours = psx.conv(i8, @rem(@divTrunc(arg2, 16777216), 256));
    }

    c.vs_battle_setStateFlag(0xA0, psx.conv(u8, st.frames));
    c.vs_battle_setStateFlag(0xA1, psx.conv(u8, st.seconds));
    c.vs_battle_setStateFlag(0xA2, psx.conv(u8, st.minutes));
    c.vs_battle_setStateFlag(0xA3, psx.conv(u8, st.hours));
    c.vs_battle_showPuzzleTimer();
}
