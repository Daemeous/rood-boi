//! The event script interpreter's step: runs opcodes through the opcode
//! table until one yields (pc/src/game/BATTLE/BATTLE.PRG/573B8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_573B8");
const c = @import("c_573B8");
const event_jump = @import("event_jump.zig");

const Op = *const fn ([*c]u8, c_short) callconv(.c) c_int;

/// orig: vs_event_executeScriptThread (pc/src/game/BATTLE/BATTLE.PRG/573B8.c)
/// Runs the event script at `*arg0` (START skips a skippable scene first); 1 when an opcode waits a frame, else 0 with the resume point stored.
pub fn vs_event_executeScriptThread(arg0: [*c][*c]u8, arg1: c_short) callconv(.c) c_short {
    var script: [*c]u8 = arg0[0];

    if (arg1 == 0 and v.g.vs_event_skipState().* == 1 and
        ((v.g.vs_main_buttonsPressed().all & (1 << 11)) != 0 or v.g.vs_event_isSkipForced().* != 0))
    {
        v.g.vs_event_isSkipForced().* = 0;
        v.g.vs_event_threadWaitFrames()[3] = 0;
        v.g.vs_event_threadWaitFrames()[2] = 0;
        v.g.vs_event_threadWaitFrames()[1] = 0;
        v.g.vs_event_threadWaitFrames()[0] = 0;
        v.g.vs_battle_textBoxStatuses()[3] = 0;
        v.g.vs_battle_textBoxStatuses()[2] = 0;
        v.g.vs_battle_textBoxStatuses()[1] = 0;
        v.g.vs_battle_textBoxStatuses()[0] = 0;

        if (v.g.vs_event_stopsMusicOnSkip().* != 0) {
            _ = c.vs_audio_stopMusicSlotPlayback(0);
        }

        c.vs_audio_stopAllSfxPlayback();
        _ = c.vs_input_stopVibrationPatterns(0, 2);
        c.vs_event_resetCutsceneActorsToIdle();
        c.vs_field_beginScreenFadeOut(0, 0xFF, 0);
        c.func_8007C424();
        c.vs_battle_setStateFlag(0xA8, 1);
        v.g.vs_event_skipState().* = 2;
    }

    const table = psx.GlobalMany(?Op, "_opcodeFunctionTable").ptr();
    const scene = psx.Global(?[*]?Op, "vs_event_legacySkipHandlerCopy").ptr();
    while (true) {
        // ROOD_PC: the opcode's callback must be native code (its low 32 bits, as the C casts it).
        const callback: ?Op = if (v.g.vs_event_skipState().* != 2) table[script[0]] else scene.*.?[script[0]];
        c.pc_require_native_callback(@truncate(@intFromPtr(callback)), "event opcode callback relocation", script[0]);

        const ret: c_int = if (v.g.vs_event_skipState().* != 2)
            table[script[0]].?(script, arg1)
        else
            scene.*.?[script[0]].?(script, arg1);

        switch (ret) {
            0 => {
                script = @ptrCast(psx.at(script, c.vs_event_opcodeSize(script)));
                continue;
            },
            1 => {},
            3 => {
                arg0[0] = null;
                return 0;
            },
            2 => return 1,
            4 => script = @ptrCast(psx.at(script, c.vs_event_opcodeSize(script))),
            event_jump.code => {
                script = event_jump.take();
                continue;
            },
            else => {
                script = @ptrFromInt(@as(usize, @as(u32, @bitCast(ret))));
                continue;
            },
        }

        arg0[0] = script;
        return 0;
    }
}
