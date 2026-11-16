//! The event script's frame (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_updateEventPlayback (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Runs the event script state machine (vs_event_playbackPhase): 0 idle; 1 waits for the start (vs_event_isStartConfirmed);
/// 2 opens the script (the room's, a door's, or loads an event file: 3 waits for the load); else
/// runs the threads, the camera and the screen effects, and ends the script when its text is done.
/// Returns 0 when no script runs, else 2.
pub fn vs_event_updateEventPlayback() callconv(.c) c_int {
    const state = v.g.vs_event_playbackPhase();
    if (state.* == 0) return 0;
    if (state.* == 1) {
        if (v.g.vs_event_isStartConfirmed().* != 0) {
            state.* = 2;
            v.g.vs_event_isStartConfirmed().* = 0;
            c.func_800BBBE8();
            c.vs_actors_returnAllActorsToIdle();
            var i: usize = 0;
            while (i < 17) : (i += 1) v.g.vs_event_scriptControlledActors()[i] = 0;
            return 2;
        }
        return 0;
    }
    if (state.* == 2) {
        v.g.D_800F4C6A().* = 0;
        v.g.vs_event_isChainedEventLoaded().* = 0;
        switch (@as(i16, @bitCast(v.g.vs_event_eventSource().*))) {
            1 => {
                c.vs_event_initializeEventScriptData(@ptrCast(v.g.vs_event_scriptBuffer().*));
                state.* = 4;
            },
            2 => {
                const doors: [*c]u8 = @ptrCast(v.g._mpdRoomDoorSection().*);
                const offsets: [*c]align(1) i16 = @ptrCast(doors);
                c.vs_event_initializeEventScriptData(psx.at(doors, psx.at(offsets, v.g._evtFile().*).*));
                state.* = 4;
            },
            0 => {
                c.vs_event_queueEventScriptLoad(v.g._evtFile().*, v.g.vs_event_scriptBuffer().*);
                state.* = 3;
            },
            else => {},
        }
        return 2;
    }
    if (state.* == 3) {
        if (v.g.vs_event_eventFileDiscRequest().*.?[0] == 4) {
            c.vs_main_freeCdQueueSlot(@ptrCast(@alignCast(v.g.vs_event_eventFileDiscRequest().*)));
            if (v.g.vs_event_isChainedEventLoaded().* != 0) {
                c.vs_main_memcpy(v.g.vs_event_scriptBuffer().*, v.g.vs_event_chainedEventBuffer().*, 0x1800);
                c.vs_main_freeHeapR(v.g.vs_event_chainedEventBuffer().*);
                v.g.vs_event_isChainedEventLoaded().* = 0;
            }
            c.vs_event_initializeEventScriptData(@ptrCast(v.g.vs_event_scriptBuffer().*));
            state.* = 4;
        }
        return 2;
    }
    c.func_800BBE94();
    if (v.g.vs_event_isMainThreadFinished().* == 0) {
        const threads: [*c][*c]u8 = @ptrCast(v.g.vs_event_threadCursors());
        if (c.vs_event_executeScriptThread(threads, 0) != 0) {
            if (v.g.vs_event_isFieldCameraRestoreScripted().* == 0 and v.g.vs_event_isChainedEventLoaded().* == 0 and v.g.vs_event_roomChangeState().* == 0) {
                c.func_800BC1CC(-1, -1);
            }
            v.g.vs_event_isMainThreadFinished().* = 1;
        }
        var i: c_short = 1;
        while (i < 4) : (i += 1) {
            if (v.g.vs_event_threadCursors()[@intCast(i)] != null) _ = c.vs_event_executeScriptThread(threads + @as(usize, @intCast(i)), i);
        }
    }
    c.func_800BC9E0();
    c.func_800BDAB4();
    c.vs_battle_applyCameraState();
    c.func_800BB9B0();
    c.func_800BBAFC();
    if (v.g.vs_event_isScanlineOverlayEnabled().* != 0) c.vs_event_drawEventScanlineOverlay();
    if (v.g.vs_event_isMainThreadFinished().* != 0 and psx.conv(u8, c.func_800BD610()) == 0xFF) {
        if (v.g.vs_event_isChainedEventLoaded().* != 0) {
            state.* = 3;
            v.g.vs_event_eventSource().* = 0;
        } else {
            c.vs_event_releaseScriptActorControl();
            state.* = 0;
        }
        v.g.vs_event_isMainThreadFinished().* = 0;
    }
    return 2;
}
